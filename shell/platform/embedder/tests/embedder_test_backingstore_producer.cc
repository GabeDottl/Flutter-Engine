// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/embedder/tests/embedder_test_backingstore_producer.h"

#include "flutter/fml/logging.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSize.h"
#include "third_party/skia/include/core/SkSurface.h"

#include <memory>

namespace flutter {
namespace testing {

EmbedderTestBackingStoreProducer::EmbedderTestBackingStoreProducer(
    sk_sp<GrDirectContext> context,
    RenderTargetType type)
    : context_(context),
      type_(type)
#ifdef SHELL_ENABLE_METAL
      ,
      test_metal_context_(std::make_unique<TestMetalContext>())
#endif
{
}

EmbedderTestBackingStoreProducer::~EmbedderTestBackingStoreProducer() = default;

bool EmbedderTestBackingStoreProducer::Create(
    const FlutterBackingStoreConfig* config,
    FlutterBackingStore* renderer_out) {
  switch (type_) {
    case RenderTargetType::kSoftwareBuffer:
      return CreateSoftware(config, renderer_out);
#ifdef SHELL_ENABLE_GL
    case RenderTargetType::kOpenGLTexture:
      return CreateTexture(config, renderer_out);
    case RenderTargetType::kOpenGLFramebuffer:
      return CreateFramebuffer(config, renderer_out);
#endif
#ifdef SHELL_ENABLE_METAL
    case RenderTargetType::kMetalTexture:
      return CreateMTLTexture(config, renderer_out);
#endif
    default:
      return false;
  }
}

bool EmbedderTestBackingStoreProducer::CreateFramebuffer(
    const FlutterBackingStoreConfig* config,
    FlutterBackingStore* backing_store_out) {
#ifdef SHELL_ENABLE_GL
  const auto image_info =
      SkImageInfo::MakeN32Premul(config->size.width, config->size.height);

  auto surface = SkSurface::MakeRenderTarget(
      context_.get(),               // context
      SkBudgeted::kNo,              // budgeted
      image_info,                   // image info
      1,                            // sample count
      kBottomLeft_GrSurfaceOrigin,  // surface origin
      nullptr,                      // surface properties
      false                         // mipmaps
  );

  if (!surface) {
    FML_LOG(ERROR) << "Could not create render target for compositor layer.";
    return false;
  }

  GrBackendRenderTarget render_target = surface->getBackendRenderTarget(
      SkSurface::BackendHandleAccess::kDiscardWrite_BackendHandleAccess);

  if (!render_target.isValid()) {
    FML_LOG(ERROR) << "Backend render target was invalid.";
    return false;
  }

  GrGLFramebufferInfo framebuffer_info = {};
  if (!render_target.getGLFramebufferInfo(&framebuffer_info)) {
    FML_LOG(ERROR) << "Could not access backend framebuffer info.";
    return false;
  }

  backing_store_out->type = kFlutterBackingStoreTypeOpenGL;
  backing_store_out->user_data = surface.get();
  backing_store_out->open_gl.type = kFlutterOpenGLTargetTypeFramebuffer;
  backing_store_out->open_gl.framebuffer.target = framebuffer_info.fFormat;
  backing_store_out->open_gl.framebuffer.name = framebuffer_info.fFBOID;
  // The balancing unref is in the destruction callback.
  surface->ref();
  backing_store_out->open_gl.framebuffer.user_data = surface.get();
  backing_store_out->open_gl.framebuffer.destruction_callback =
      [](void* user_data) { reinterpret_cast<SkSurface*>(user_data)->unref(); };

  return true;
#else
  return false;
#endif
}

bool EmbedderTestBackingStoreProducer::CreateTexture(
    const FlutterBackingStoreConfig* config,
    FlutterBackingStore* backing_store_out) {
#ifdef SHELL_ENABLE_GL
  const auto image_info =
      SkImageInfo::MakeN32Premul(config->size.width, config->size.height);

  auto surface = SkSurface::MakeRenderTarget(
      context_.get(),               // context
      SkBudgeted::kNo,              // budgeted
      image_info,                   // image info
      1,                            // sample count
      kBottomLeft_GrSurfaceOrigin,  // surface origin
      nullptr,                      // surface properties
      false                         // mipmaps
  );

  if (!surface) {
    FML_LOG(ERROR) << "Could not create render target for compositor layer.";
    return false;
  }

  GrBackendTexture render_texture = surface->getBackendTexture(
      SkSurface::BackendHandleAccess::kDiscardWrite_BackendHandleAccess);

  if (!render_texture.isValid()) {
    FML_LOG(ERROR) << "Backend render texture was invalid.";
    return false;
  }

  GrGLTextureInfo texture_info = {};
  if (!render_texture.getGLTextureInfo(&texture_info)) {
    FML_LOG(ERROR) << "Could not access backend texture info.";
    return false;
  }

  backing_store_out->type = kFlutterBackingStoreTypeOpenGL;
  backing_store_out->user_data = surface.get();
  backing_store_out->open_gl.type = kFlutterOpenGLTargetTypeTexture;
  backing_store_out->open_gl.texture.target = texture_info.fTarget;
  backing_store_out->open_gl.texture.name = texture_info.fID;
  backing_store_out->open_gl.texture.format = texture_info.fFormat;
  // The balancing unref is in the destruction callback.
  surface->ref();
  backing_store_out->open_gl.texture.user_data = surface.get();
  backing_store_out->open_gl.texture.destruction_callback =
      [](void* user_data) { reinterpret_cast<SkSurface*>(user_data)->unref(); };

  return true;
#else
  return false;
#endif
}

bool EmbedderTestBackingStoreProducer::CreateSoftware(
    const FlutterBackingStoreConfig* config,
    FlutterBackingStore* backing_store_out) {
  auto surface = SkSurface::MakeRaster(
      SkImageInfo::MakeN32Premul(config->size.width, config->size.height));

  if (!surface) {
    FML_LOG(ERROR)
        << "Could not create the render target for compositor layer.";
    return false;
  }

  SkPixmap pixmap;
  if (!surface->peekPixels(&pixmap)) {
    FML_LOG(ERROR) << "Could not peek pixels of pixmap.";
    return false;
  }

  backing_store_out->type = kFlutterBackingStoreTypeSoftware;
  backing_store_out->user_data = surface.get();
  backing_store_out->software.allocation = pixmap.addr();
  backing_store_out->software.row_bytes = pixmap.rowBytes();
  backing_store_out->software.height = pixmap.height();
  // The balancing unref is in the destruction callback.
  surface->ref();
  backing_store_out->software.user_data = surface.get();
  backing_store_out->software.destruction_callback = [](void* user_data) {
    reinterpret_cast<SkSurface*>(user_data)->unref();
  };

  return true;
}

bool EmbedderTestBackingStoreProducer::CreateMTLTexture(
    const FlutterBackingStoreConfig* config,
    FlutterBackingStore* backing_store_out) {
#ifdef SHELL_ENABLE_METAL
  // TODO(gw280): Use SkSurface::MakeRenderTarget instead of generating our
  // own MTLTexture and wrapping it.
  auto surface_size = SkISize::Make(config->size.width, config->size.height);
  auto texture_info = test_metal_context_->CreateMetalTexture(surface_size);

  GrMtlTextureInfo skia_texture_info;
  skia_texture_info.fTexture.reset(SkCFSafeRetain(texture_info.texture));
  GrBackendTexture backend_texture(surface_size.width(), surface_size.height(),
                                   GrMipmapped::kNo, skia_texture_info);

  sk_sp<SkSurface> surface = SkSurface::MakeFromBackendTexture(
      context_.get(), backend_texture, kTopLeft_GrSurfaceOrigin, 1,
      kBGRA_8888_SkColorType, nullptr, nullptr);

  if (!surface) {
    FML_LOG(ERROR) << "Could not create Skia surface from a Metal texture.";
    return false;
  }

  backing_store_out->type = kFlutterBackingStoreTypeMetal;
  backing_store_out->user_data = surface.get();
  backing_store_out->metal.texture.texture = texture_info.texture;
  // The balancing unref is in the destruction callback.
  surface->ref();
  backing_store_out->metal.struct_size = sizeof(FlutterMetalBackingStore);
  backing_store_out->metal.texture.user_data = surface.get();
  backing_store_out->metal.texture.destruction_callback = [](void* user_data) {
    reinterpret_cast<SkSurface*>(user_data)->unref();
  };

  return true;
#else
  return false;
#endif
}

}  // namespace testing
}  // namespace flutter
