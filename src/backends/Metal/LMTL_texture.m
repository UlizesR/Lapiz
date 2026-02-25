#import "Lapiz/backends/Metal/LMTL.h"
#import "Lapiz/graphics/Ltexture.h"
#import "Lapiz/core/Lerror.h"

#import <Metal/Metal.h>
#import <stdlib.h>

LapizTexture* LapizMTLTextureCreateFromPixels(int width, int height, const unsigned char* pixels)
{
    if (width <= 0 || height <= 0) return NULL;

    if (!mtl_s || !mtl_s->device) {
        LapizSetError(&L_State.error, LAPIZ_ERROR_FAILED, "Metal context not initialized");
        return NULL;
    }

    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                width:(NSUInteger)width
                                                                               height:(NSUInteger)height
                                                                            mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeShared;

    id<MTLTexture> mtlTex = [mtl_s->device newTextureWithDescriptor:desc];
    if (!mtlTex) {
        LapizSetError(&L_State.error, LAPIZ_ERROR_METAL_ERROR, "Failed to create Metal texture");
        return NULL;
    }

    if (pixels)
    {
        MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger)width, (NSUInteger)height);
        NSUInteger bytesPerRow = (NSUInteger)width * 4;
        [mtlTex replaceRegion:region mipmapLevel:0 withBytes:pixels bytesPerRow:bytesPerRow];
    }

    LapizTexture* tex = (LapizTexture*)calloc(1, sizeof(LapizTexture));
    if (!tex) {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate texture");
        return NULL;
    }

    tex->_backend = (__bridge void*)mtlTex;
    tex->width = width;
    tex->height = height;
    return tex;
}

void LapizMTLTextureUnload(LapizTexture* texture)
{
    if (!texture) return;
    if (texture->_backend)
    {
        id<MTLTexture> mtlTex = (__bridge_transfer id<MTLTexture>)texture->_backend;
        (void)mtlTex;
        texture->_backend = NULL;
    }
    texture->width = texture->height = 0;
}
