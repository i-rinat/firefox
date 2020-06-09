/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "X11PixmapImage.h"

#include "mozilla/layers/TextureClient.h"
#include "mozilla/layers/TextureClientX11.h"

namespace mozilla::layers {

class MOZ_RAII X11PixmapTextureClientAllocationHelper
    : public ITextureClientAllocationHelper {
 public:
  X11PixmapTextureClientAllocationHelper(gfx::SurfaceFormat aFormat,
                                         const gfx::IntSize& aSize,
                                         TextureAllocationFlags aAllocFlags,
                                         TextureFlags aTextureFlags)
      : ITextureClientAllocationHelper(aFormat, aSize, BackendSelector::Content,
                                       aTextureFlags, aAllocFlags) {}
  bool IsCompatible(TextureClient* aTextureClient) override {
    return aTextureClient->GetFormat() == mFormat &&
           aTextureClient->GetSize() == mSize;
  }

  already_AddRefed<TextureClient> Allocate(
      KnowsCompositor* aAllocator) override {
    if (!aAllocator) {
      return nullptr;
    }
    X11TextureData* data = X11TextureData::CreateX11Drawable(
        mSize, mFormat, mTextureFlags, aAllocator->GetTextureForwarder());
    if (!data) {
      return nullptr;
    }

    return MakeAndAddRef<TextureClient>(data, mTextureFlags,
                                        aAllocator->GetTextureForwarder());
  }
};

X11PixmapRecycleAllocator::X11PixmapRecycleAllocator(
    KnowsCompositor* aAllocator, void* placeholder1)
    : TextureClientRecycleAllocator(aAllocator) {}

already_AddRefed<TextureClient>
X11PixmapRecycleAllocator::CreateOrRecycleClient(const gfx::IntSize& aSize) {
  X11PixmapTextureClientAllocationHelper helper(
      gfx::SurfaceFormat::X8R8G8B8_UINT32, aSize,
      TextureAllocationFlags::ALLOC_DEFAULT,
      layers::TextureFlags::DEALLOCATE_CLIENT);
  RefPtr<TextureClient> textureClient = CreateOrRecycle(helper);
  return textureClient.forget();
}

}  // namespace mozilla::layers
