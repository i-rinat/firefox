/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositorAnimationStorage.h"

#include "AnimationHelper.h"
#include "mozilla/layers/CompositorThread.h"  // for CompositorThreadHolder
#include "mozilla/layers/LayerManagerComposite.h"  // for LayerComposite, etc
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/webrender/WebRenderTypes.h"  // for ToWrTransformProperty, etc
#include "nsDeviceContext.h"  // for AppUnitsPerCSSPixel
#include "nsDisplayList.h"    // for nsDisplayTransform, etc
#include "TreeTraversal.h"  // for ForEachNode, BreadthFirstSearch

namespace mozilla {
namespace layers {

void CompositorAnimationStorage::Clear() {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  // This function should only be called via the non Webrender version of
  // SampleAnimations.
  mLock.AssertCurrentThreadOwns();

  mAnimatedValues.Clear();
  mAnimations.clear();
}

void CompositorAnimationStorage::ClearById(const uint64_t& aId) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MutexAutoLock lock(mLock);

  mAnimatedValues.Remove(aId);
  mAnimations.erase(aId);
}

bool CompositorAnimationStorage::HasAnimations() const {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MutexAutoLock lock(mLock);

  return !mAnimations.empty();
}

AnimatedValue* CompositorAnimationStorage::GetAnimatedValue(
    const uint64_t& aId) const {
  mLock.AssertCurrentThreadOwns();

  return mAnimatedValues.Get(aId);
}

OMTAValue CompositorAnimationStorage::GetOMTAValue(const uint64_t& aId) const {
  MutexAutoLock lock(mLock);

  OMTAValue omtaValue = mozilla::null_t();
  auto animatedValue = GetAnimatedValue(aId);
  if (!animatedValue) {
    return omtaValue;
  }

  animatedValue->Value().match(
      [&](const AnimationTransform& aTransform) {
        gfx::Matrix4x4 transform = aTransform.mFrameTransform;
        const TransformData& data = aTransform.mData;
        float scale = data.appUnitsPerDevPixel();
        gfx::Point3D transformOrigin = data.transformOrigin();

        // Undo the rebasing applied by
        // nsDisplayTransform::GetResultingTransformMatrixInternal
        transform.ChangeBasis(-transformOrigin);

        // Convert to CSS pixels (this undoes the operations performed by
        // nsStyleTransformMatrix::ProcessTranslatePart which is called from
        // nsDisplayTransform::GetResultingTransformMatrix)
        double devPerCss = double(scale) / double(AppUnitsPerCSSPixel());
        transform._41 *= devPerCss;
        transform._42 *= devPerCss;
        transform._43 *= devPerCss;
        omtaValue = transform;
      },
      [&](const float& aOpacity) { omtaValue = aOpacity; },
      [&](const nscolor& aColor) { omtaValue = aColor; });
  return omtaValue;
}

void CompositorAnimationStorage::SetAnimatedValue(
    uint64_t aId, AnimatedValue* aPreviousValue,
    gfx::Matrix4x4&& aTransformInDevSpace, gfx::Matrix4x4&& aFrameTransform,
    const TransformData& aData) {
  mLock.AssertCurrentThreadOwns();

  if (!aPreviousValue) {
    MOZ_ASSERT(!mAnimatedValues.Contains(aId));
    mAnimatedValues.Put(
        aId, MakeUnique<AnimatedValue>(std::move(aTransformInDevSpace),
                                       std::move(aFrameTransform), aData));
    return;
  }
  MOZ_ASSERT(aPreviousValue->Is<AnimationTransform>());
  MOZ_ASSERT(aPreviousValue == GetAnimatedValue(aId));

  aPreviousValue->SetTransform(std::move(aTransformInDevSpace),
                               std::move(aFrameTransform), aData);
}

void CompositorAnimationStorage::SetAnimatedValue(uint64_t aId,
                                                  AnimatedValue* aPreviousValue,
                                                  nscolor aColor) {
  mLock.AssertCurrentThreadOwns();

  if (!aPreviousValue) {
    MOZ_ASSERT(!mAnimatedValues.Contains(aId));
    mAnimatedValues.Put(aId, MakeUnique<AnimatedValue>(aColor));
    return;
  }

  MOZ_ASSERT(aPreviousValue->Is<nscolor>());
  MOZ_ASSERT(aPreviousValue == GetAnimatedValue(aId));
  aPreviousValue->SetColor(aColor);
}

void CompositorAnimationStorage::SetAnimatedValue(uint64_t aId,
                                                  AnimatedValue* aPreviousValue,
                                                  float aOpacity) {
  mLock.AssertCurrentThreadOwns();

  if (!aPreviousValue) {
    MOZ_ASSERT(!mAnimatedValues.Contains(aId));
    mAnimatedValues.Put(aId, MakeUnique<AnimatedValue>(aOpacity));
    return;
  }

  MOZ_ASSERT(aPreviousValue->Is<float>());
  MOZ_ASSERT(aPreviousValue == GetAnimatedValue(aId));
  aPreviousValue->SetOpacity(aOpacity);
}

void CompositorAnimationStorage::SetAnimations(uint64_t aId,
                                               const AnimationArray& aValue) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MutexAutoLock lock(mLock);

  mAnimations[aId] = std::make_unique<AnimationStorageData>(
      AnimationHelper::ExtractAnimations(aValue));

  // If there is the last animated value, then we need to store the id to remove
  // the value if the new animation doesn't produce any animated data (i.e. in
  // the delay phase) when we sample this new animation.
  if (mAnimatedValues.Contains(aId)) {
    mNewAnimations.insert(aId);
  }
}

bool CompositorAnimationStorage::SampleAnimations(TimeStamp aPreviousFrameTime,
                                                  TimeStamp aCurrentFrameTime) {
  MutexAutoLock lock(mLock);

  bool isAnimating = false;
  auto cleanup = MakeScopeExit([&] { mNewAnimations.clear(); });

  // Do nothing if there are no compositor animations
  if (mAnimations.empty()) {
    return isAnimating;
  }

  for (const auto& iter : mAnimations) {
    const auto& animationStorageData = iter.second;
    if (animationStorageData->mAnimation.IsEmpty()) {
      continue;
    }

    isAnimating = true;
    AutoTArray<RefPtr<RawServoAnimationValue>, 1> animationValues;
    AnimatedValue* previousValue = GetAnimatedValue(iter.first);
    AnimationHelper::SampleResult sampleResult =
        AnimationHelper::SampleAnimationForEachNode(
            aPreviousFrameTime, aCurrentFrameTime, previousValue,
            animationStorageData->mAnimation, animationValues);

    if (sampleResult != AnimationHelper::SampleResult::Sampled) {
      if (mNewAnimations.find(iter.first) != mNewAnimations.end()) {
        mAnimatedValues.Remove(iter.first);
      }
      continue;
    }

    const PropertyAnimationGroup& lastPropertyAnimationGroup =
        animationStorageData->mAnimation.LastElement();

    // Store the AnimatedValue
    switch (lastPropertyAnimationGroup.mProperty) {
      case eCSSProperty_background_color: {
        SetAnimatedValue(iter.first, previousValue,
                         Servo_AnimationValue_GetColor(animationValues[0],
                                                       NS_RGBA(0, 0, 0, 0)));
        break;
      }
      case eCSSProperty_opacity: {
        MOZ_ASSERT(animationValues.Length() == 1);
        SetAnimatedValue(iter.first, previousValue,
                         Servo_AnimationValue_GetOpacity(animationValues[0]));
        break;
      }
      case eCSSProperty_rotate:
      case eCSSProperty_scale:
      case eCSSProperty_translate:
      case eCSSProperty_transform:
      case eCSSProperty_offset_path:
      case eCSSProperty_offset_distance:
      case eCSSProperty_offset_rotate:
      case eCSSProperty_offset_anchor: {
        MOZ_ASSERT(animationStorageData->mTransformData);

        const TransformData& transformData =
            *animationStorageData->mTransformData;
        MOZ_ASSERT(transformData.origin() == nsPoint());

        gfx::Matrix4x4 transform =
            AnimationHelper::ServoAnimationValueToMatrix4x4(
                animationValues, transformData,
                animationStorageData->mCachedMotionPath);
        gfx::Matrix4x4 frameTransform = transform;

        transform.PostScale(transformData.inheritedXScale(),
                            transformData.inheritedYScale(), 1);

        SetAnimatedValue(iter.first, previousValue, std::move(transform),
                         std::move(frameTransform), transformData);
        break;
      }
      default:
        MOZ_ASSERT_UNREACHABLE("Unhandled animated property");
    }
  }

  return isAnimating;
}

WrAnimations CompositorAnimationStorage::CollectWebRenderAnimations() const {
  MutexAutoLock lock(mLock);

  WrAnimations animations;

  for (auto iter = mAnimatedValues.ConstIter(); !iter.Done(); iter.Next()) {
    AnimatedValue* value = iter.UserData();
    value->Value().match(
        [&](const AnimationTransform& aTransform) {
          animations.mTransformArrays.AppendElement(wr::ToWrTransformProperty(
              iter.Key(), aTransform.mTransformInDevSpace));
        },
        [&](const float& aOpacity) {
          animations.mOpacityArrays.AppendElement(
              wr::ToWrOpacityProperty(iter.Key(), aOpacity));
        },
        [&](const nscolor& aColor) {
          animations.mColorArrays.AppendElement(wr::ToWrColorProperty(
              iter.Key(), ToDeviceColor(gfx::sRGBColor::FromABGR(aColor))));
        });
  }

  return animations;
}

static gfx::Matrix4x4 FrameTransformToTransformInDevice(
    const gfx::Matrix4x4& aFrameTransform, Layer* aLayer,
    const TransformData& aTransformData) {
  gfx::Matrix4x4 transformInDevice = aFrameTransform;
  // If our parent layer is a perspective layer, then the offset into reference
  // frame coordinates is already on that layer. If not, then we need to ask
  // for it to be added here.
  if (!aLayer->GetParent() ||
      !aLayer->GetParent()->GetTransformIsPerspective()) {
    nsLayoutUtils::PostTranslate(
        transformInDevice, aTransformData.origin(),
        aTransformData.appUnitsPerDevPixel(),
        aLayer->GetContentFlags() & Layer::CONTENT_SNAP_TO_GRID);
  }

  if (ContainerLayer* c = aLayer->AsContainerLayer()) {
    transformInDevice.PostScale(c->GetInheritedXScale(),
                                c->GetInheritedYScale(), 1);
  }

  return transformInDevice;
}

void CompositorAnimationStorage::ApplyAnimatedValue(
    Layer* aLayer, nsCSSPropertyID aProperty, AnimatedValue* aPreviousValue,
    const nsTArray<RefPtr<RawServoAnimationValue>>& aValues) {
  mLock.AssertCurrentThreadOwns();

  MOZ_ASSERT(!aValues.IsEmpty());

  HostLayer* layerCompositor = aLayer->AsHostLayer();
  switch (aProperty) {
    case eCSSProperty_background_color: {
      MOZ_ASSERT(aValues.Length() == 1);
      // We don't support 'color' animations on the compositor yet so we never
      // meet currentColor on the compositor.
      nscolor color =
          Servo_AnimationValue_GetColor(aValues[0], NS_RGBA(0, 0, 0, 0));
      aLayer->AsColorLayer()->SetColor(gfx::ToDeviceColor(color));
      SetAnimatedValue(aLayer->GetCompositorAnimationsId(), aPreviousValue,
                       color);

      layerCompositor->SetShadowOpacity(aLayer->GetOpacity());
      layerCompositor->SetShadowOpacitySetByAnimation(false);
      layerCompositor->SetShadowBaseTransform(aLayer->GetBaseTransform());
      layerCompositor->SetShadowTransformSetByAnimation(false);
      break;
    }
    case eCSSProperty_opacity: {
      MOZ_ASSERT(aValues.Length() == 1);
      float opacity = Servo_AnimationValue_GetOpacity(aValues[0]);
      layerCompositor->SetShadowOpacity(opacity);
      layerCompositor->SetShadowOpacitySetByAnimation(true);
      SetAnimatedValue(aLayer->GetCompositorAnimationsId(), aPreviousValue,
                       opacity);

      layerCompositor->SetShadowBaseTransform(aLayer->GetBaseTransform());
      layerCompositor->SetShadowTransformSetByAnimation(false);
      break;
    }
    case eCSSProperty_rotate:
    case eCSSProperty_scale:
    case eCSSProperty_translate:
    case eCSSProperty_transform:
    case eCSSProperty_offset_path:
    case eCSSProperty_offset_distance:
    case eCSSProperty_offset_rotate:
    case eCSSProperty_offset_anchor: {
      MOZ_ASSERT(aLayer->GetTransformData());
      const TransformData& transformData = *aLayer->GetTransformData();
      gfx::Matrix4x4 frameTransform =
          AnimationHelper::ServoAnimationValueToMatrix4x4(
              aValues, transformData, aLayer->CachedMotionPath());

      gfx::Matrix4x4 transform = FrameTransformToTransformInDevice(
          frameTransform, aLayer, transformData);

      layerCompositor->SetShadowBaseTransform(transform);
      layerCompositor->SetShadowTransformSetByAnimation(true);
      SetAnimatedValue(aLayer->GetCompositorAnimationsId(), aPreviousValue,
                       std::move(transform), std::move(frameTransform),
                       transformData);

      layerCompositor->SetShadowOpacity(aLayer->GetOpacity());
      layerCompositor->SetShadowOpacitySetByAnimation(false);
      break;
    }
    default:
      MOZ_ASSERT_UNREACHABLE("Unhandled animated property");
  }
}

bool CompositorAnimationStorage::SampleAnimations(Layer* aRoot,
                                                  TimeStamp aPreviousFrameTime,
                                                  TimeStamp aCurrentFrameTime) {
  MutexAutoLock lock(mLock);

  bool isAnimating = false;

  auto autoClearAnimationStorage = MakeScopeExit([&] {
    if (!isAnimating) {
      // Clean up the CompositorAnimationStorage because
      // there are no active animations running
      Clear();
    }
  });

  ForEachNode<ForwardIterator>(aRoot, [&](Layer* layer) {
    auto& propertyAnimationGroups = layer->GetPropertyAnimationGroups();
    if (propertyAnimationGroups.IsEmpty()) {
      return;
    }

    isAnimating = true;
    AnimatedValue* previousValue =
        GetAnimatedValue(layer->GetCompositorAnimationsId());

    AutoTArray<RefPtr<RawServoAnimationValue>, 1> animationValues;
    AnimationHelper::SampleResult sampleResult =
        AnimationHelper::SampleAnimationForEachNode(
            aPreviousFrameTime, aCurrentFrameTime, previousValue,
            propertyAnimationGroups, animationValues);

    const PropertyAnimationGroup& lastPropertyAnimationGroup =
        propertyAnimationGroups.LastElement();

    switch (sampleResult) {
      case AnimationHelper::SampleResult::Sampled:
        // We assume all transform like properties (on the same frame) live in
        // a single same layer, so using the transform data of the last element
        // should be fine.
        ApplyAnimatedValue(layer, lastPropertyAnimationGroup.mProperty,
                           previousValue, animationValues);
        break;
      case AnimationHelper::SampleResult::Skipped:
        switch (lastPropertyAnimationGroup.mProperty) {
          case eCSSProperty_background_color:
          case eCSSProperty_opacity: {
            if (lastPropertyAnimationGroup.mProperty == eCSSProperty_opacity) {
              MOZ_ASSERT(
                  layer->AsHostLayer()->GetShadowOpacitySetByAnimation());
#ifdef DEBUG
              // Disable this assertion until the root cause is fixed in bug
              // 1459775.
              // MOZ_ASSERT(FuzzyEqualsMultiplicative(
              //   Servo_AnimationValue_GetOpacity(animationValue),
              //   *(GetAnimationOpacity(layer->GetCompositorAnimationsId()))));
#endif
            }
            // Even if opacity or background-color  animation value has
            // unchanged, we have to set the shadow base transform value
            // here since the value might have been changed by APZC.
            HostLayer* layerCompositor = layer->AsHostLayer();
            layerCompositor->SetShadowBaseTransform(layer->GetBaseTransform());
            layerCompositor->SetShadowTransformSetByAnimation(false);
            break;
          }
          case eCSSProperty_rotate:
          case eCSSProperty_scale:
          case eCSSProperty_translate:
          case eCSSProperty_transform:
          case eCSSProperty_offset_path:
          case eCSSProperty_offset_distance:
          case eCSSProperty_offset_rotate:
          case eCSSProperty_offset_anchor: {
            MOZ_ASSERT(
                layer->AsHostLayer()->GetShadowTransformSetByAnimation());
            MOZ_ASSERT(previousValue);
            MOZ_ASSERT(layer->GetTransformData());
#ifdef DEBUG
            gfx::Matrix4x4 frameTransform =
                AnimationHelper::ServoAnimationValueToMatrix4x4(
                    animationValues, *layer->GetTransformData(),
                    layer->CachedMotionPath());
            gfx::Matrix4x4 transformInDevice =
                FrameTransformToTransformInDevice(frameTransform, layer,
                                                  *layer->GetTransformData());
            MOZ_ASSERT(previousValue->Transform()
                           .mTransformInDevSpace.FuzzyEqualsMultiplicative(
                               transformInDevice));
#endif
            // In the case of transform we have to set the unchanged
            // transform value again because APZC might have modified the
            // previous shadow base transform value.
            HostLayer* layerCompositor = layer->AsHostLayer();
            layerCompositor->SetShadowBaseTransform(
                // FIXME: Bug 1459775: It seems possible that we somehow try
                // to sample animations and skip it even if the previous value
                // has been discarded from the animation storage when we enable
                // layer tree cache. So for the safety, in the case where we
                // have no previous animation value, we set non-animating value
                // instead.
                previousValue ? previousValue->Transform().mTransformInDevSpace
                              : layer->GetBaseTransform());
            break;
          }
          default:
            MOZ_ASSERT_UNREACHABLE("Unsupported properties");
            break;
        }
        break;
      case AnimationHelper::SampleResult::None: {
        HostLayer* layerCompositor = layer->AsHostLayer();
        layerCompositor->SetShadowBaseTransform(layer->GetBaseTransform());
        layerCompositor->SetShadowTransformSetByAnimation(false);
        layerCompositor->SetShadowOpacity(layer->GetOpacity());
        layerCompositor->SetShadowOpacitySetByAnimation(false);
        break;
      }
      default:
        break;
    }
  });

  return isAnimating;
}

}  // namespace layers
}  // namespace mozilla
