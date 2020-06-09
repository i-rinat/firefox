/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_X11_PIXMAP_IMAGE_H
#define GFX_X11_PIXMAP_IMAGE_H

#include "mozilla/RefPtr.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/layers/TextureClient.h"
#include "mozilla/layers/TextureClientRecycleAllocator.h"

namespace mozilla::layers {

class X11PixmapRecycleAllocator final : public TextureClientRecycleAllocator {
 public:
  X11PixmapRecycleAllocator(KnowsCompositor* aAllocator, void* placeholder1);

  already_AddRefed<TextureClient> CreateOrRecycleClient(
      const gfx::IntSize& aSize);
};

}  // namespace mozilla::layers

#endif  // GFX_X11_PIXMAP_IMAGE_H
