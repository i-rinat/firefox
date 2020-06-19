/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxXlibSurface.h"
#include "GLContext.h"
#include "RenderX11TextureHost.h"

namespace mozilla {
namespace wr {

RenderX11TextureHost::RenderX11TextureHost(gfxXlibSurface* aSurface)
    : mSurface(aSurface), mGLTexture(0) {
  MOZ_COUNT_CTOR_INHERITED(RenderX11TextureHost, RenderTextureHost);
}

RenderX11TextureHost::~RenderX11TextureHost() {
  MOZ_COUNT_DTOR_INHERITED(RenderX11TextureHost, RenderTextureHost);
}

wr::WrExternalImage RenderX11TextureHost::Lock(uint8_t aChannelIndex,
                                               gl::GLContext* aGL,
                                               wr::ImageRendering aRendering) {
  if (mGL.get() != aGL) {
    if (mGL) {
      MOZ_ASSERT_UNREACHABLE("Unexpected GL context");
      return InvalidToWrExternalImage();
    }
    mGL = aGL;
  }

  if (!mGL || !mGL->MakeCurrent()) {
    return InvalidToWrExternalImage();
  }

  bool bindTexture = IsFilterUpdateNecessary(aRendering);

  if (!mGLTexture) {
    mGL->fGenTextures(1, &mGLTexture);
    bindTexture = true;
  }

  if (bindTexture) {
    mCachedRendering = aRendering;
    ActivateBindAndTexParameteri(mGL, LOCAL_GL_TEXTURE0, LOCAL_GL_TEXTURE_2D,
                                 mGLTexture, aRendering);
  }

  gl::sGLXLibrary.BindTexImage(mSurface->XDisplay(), mSurface->GetGLXPixmap());

  gfx::IntSize surfaceSize = mSurface->GetSize();
  return NativeTextureToWrExternalImage(mGLTexture, 0, 0, surfaceSize.width,
                                        surfaceSize.height);
}

void RenderX11TextureHost::Unlock() {
  if (!mGLTexture || !mGL || !mGL->MakeCurrent()) {
    return;
  }

  gl::sGLXLibrary.ReleaseTexImage(mSurface->XDisplay(),
                                  mSurface->GetGLXPixmap());
}

void RenderX11TextureHost::ClearCachedResources() {
  if (mGLTexture) {
    if (mGL && mGL->MakeCurrent()) {
      mGL->fDeleteTextures(1, &mGLTexture);
      mGLTexture = 0;
    }
  }
  mGL = nullptr;
}

}  // namespace wr
}  // namespace mozilla
