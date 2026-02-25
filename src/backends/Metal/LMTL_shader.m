#import "Lapiz/backends/Metal/LMTL.h"
#import "Lapiz/graphics/Lshader.h"
#import "Lapiz/graphics/Ltexture.h"
#import "Lapiz/core/Lio.h"
#import "Lapiz/core/Lerror.h"

#import <Metal/Metal.h>
#import <stdlib.h>
#import <string.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static const char* default_msl =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Uniforms { float4 colDiffuse; };\n"
    "struct VertexOut { float4 position [[position]]; float4 color; };\n"
    "vertex VertexOut vertexMain(uint vid [[vertex_id]]) {\n"
    "    float2 positions[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };\n"
    "    VertexOut out;\n"
    "    out.position = float4(positions[vid], 0.0, 1.0);\n"
    "    out.color = float4(1.0);\n"
    "    return out;\n"
    "}\n"
    "fragment float4 fragmentMain(VertexOut in [[stage_in]], constant Uniforms& u [[buffer(0)]]) {\n"
    "    return u.colDiffuse * in.color;\n"
    "}\n";

_Thread_local static char g_metal_shader_error[LAPIZ_SHADER_ERROR_MAX];

#define LAPIZ_UNIFORM_HASH_EMPTY -1

typedef struct { char name[32]; int offset; } MetalUniformLoc;

typedef struct {
    MetalUniformLoc* entries;
    int count;
    int hashTable[LAPIZ_UNIFORM_HASH_SIZE];
} MetalUniformHash;

static int MetalHashLookup(const MetalUniformHash* h, const char* name)
{
    uint32_t idx = LapizHashFNV1a(name) % LAPIZ_UNIFORM_HASH_SIZE;
    
    for (int probe = 0; probe < LAPIZ_UNIFORM_HASH_SIZE; probe++)
    {
        int ei = h->hashTable[idx];
        
        if (ei == LAPIZ_UNIFORM_HASH_EMPTY) 
            return -1;
        if (strcmp(h->entries[ei].name, name) == 0)
            return h->entries[ei].offset;
        
        idx = (idx + 1) % LAPIZ_UNIFORM_HASH_SIZE;
    }
    return -1;
}

struct LapizShader {
    id<MTLRenderPipelineState> pipeline;
    id<MTLBuffer> uniformBuffer;
    id<MTLBuffer> vertexUniformBuffer;
    NSUInteger colorOffset;
    MetalUniformHash uniformHash;
    MetalUniformHash vertexUniformHash;
};

static void MetalBuildUniformHash(MetalUniformHash* outHash, NSArray<MTLArgument*>* args, NSUInteger* outColorOffset)
{
    outHash->entries = NULL;
    outHash->count = 0;
    for (int i = 0; i < LAPIZ_UNIFORM_HASH_SIZE; i++)
        outHash->hashTable[i] = LAPIZ_UNIFORM_HASH_EMPTY;
    
    if (!args) 
        return;

    for (MTLArgument* arg in args)
    {
        if (arg.type != MTLArgumentTypeBuffer || arg.bufferDataType != MTLDataTypeStruct)
            continue;

        MTLStructType* st = arg.bufferStructType;
        
        if (!st || st.members.count == 0) 
            continue;

        int cnt = (int)st.members.count;
        MetalUniformLoc* entries = (MetalUniformLoc*)calloc((size_t)cnt, sizeof(MetalUniformLoc));
        
        if (!entries) 
            return;

        outHash->entries = entries;
        for (MTLStructMember* member in st.members)
        {
            if (outHash->count >= cnt) 
                break;

            const char* n = [member.name UTF8String];

            if (!n) 
                continue;

            int offset = (int)member.offset;
            
            if (outColorOffset && strcmp(n, LAPIZ_SHADER_UNIFORM_COLOR) == 0)
                *outColorOffset = (NSUInteger)offset;

            uint32_t idx = LapizHashFNV1a(n) % LAPIZ_UNIFORM_HASH_SIZE;

            while (outHash->hashTable[idx] != LAPIZ_UNIFORM_HASH_EMPTY)
                idx = (idx + 1) % LAPIZ_UNIFORM_HASH_SIZE;

            outHash->hashTable[idx] = outHash->count;
            size_t len = strlen(n);

            if (len >= 31) 
                len = 31;

            memcpy(entries[outHash->count].name, n, len);
            entries[outHash->count].name[len] = '\0';
            entries[outHash->count].offset = offset;
            outHash->count++;
        }
        break;
    }
}

static const char* kMetalVertexEntry = "vertexMain";
static const char* kMetalFragmentEntry = "fragmentMain";

LapizShader* LapizMTLShaderLoadFromMemory(const char* vsCode, const char* fsCode);
LapizShader* LapizMTLShaderLoadFromFileEx(const char* vertPath, const char* fragPath, const char* vertEntry, const char* fragEntry);
void LapizMTLShaderSetTextureEx(LapizShader* shader, int loc, LapizTexture* texture, int slot);

LapizShader* LapizMTLShaderLoadDefault(void)
{
    return LapizMTLShaderLoadFromMemory(default_msl, NULL);
}

static LapizShader* load_from_msl_source(const char* source, const char* vertEntry, const char* fragEntry)
{
    if (!source || !mtl_s || !mtl_s->device) 
        return NULL;

    const char* vEntry = vertEntry ? vertEntry : kMetalVertexEntry;
    const char* fEntry = fragEntry ? fragEntry : kMetalFragmentEntry;

    NSError* err = nil;
    g_metal_shader_error[0] = '\0';
    id<MTLLibrary> lib = [mtl_s->device newLibraryWithSource:[NSString stringWithUTF8String:source] options:nil error:&err];
    
    if (!lib)
    {
        const char* msg = err ? [[err localizedDescription] UTF8String] : "Unknown Metal library error";
        if (msg) 
        { 
            strncpy(g_metal_shader_error, msg, LAPIZ_SHADER_ERROR_MAX - 1); 
            g_metal_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0'; 
        }
        LapizSetError(&L_State.error, LAPIZ_ERROR_METAL_ERROR, msg ? msg : "Unknown Metal library error");
        LAPIZ_PRINT_ERROR(LAPIZ_ERROR_METAL_ERROR, msg);
        return NULL;
    }

    id<MTLFunction> vs = [lib newFunctionWithName:@(vEntry)];
    id<MTLFunction> fs = [lib newFunctionWithName:@(fEntry)];
    
    if (!vs || !fs) 
        return NULL;

    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction = vs;
    desc.fragmentFunction = fs;
    desc.colorAttachments[0].pixelFormat = mtl_s->colorFormat;

    MTLRenderPipelineReflection* refl = nil;
    MTLPipelineOption opts = MTLPipelineOptionArgumentInfo | MTLPipelineOptionBufferTypeInfo;
    id<MTLRenderPipelineState> pipeline = [mtl_s->device newRenderPipelineStateWithDescriptor:desc
                                                                                      options:opts
                                                                                   reflection:&refl
                                                                                        error:&err];
    if (!pipeline)
    {
        const char* msg = err ? [[err localizedDescription] UTF8String] : "Unknown Metal pipeline error";
        
        if (msg) 
        { 
            strncpy(g_metal_shader_error, msg, LAPIZ_SHADER_ERROR_MAX - 1); 
            g_metal_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0'; 
        }
        LapizSetError(&L_State.error, LAPIZ_ERROR_METAL_ERROR, msg ? msg : "Unknown Metal pipeline error");
        LAPIZ_PRINT_ERROR(LAPIZ_ERROR_METAL_ERROR, msg);
        return NULL;
    }

    NSUInteger colorOffset = 0;
    size_t uniformBufSize = (source == default_msl) ? 16 : 256;
    id<MTLBuffer> uniformBuffer = [mtl_s->device newBufferWithLength:uniformBufSize options:MTLResourceStorageModeShared];

    id<MTLBuffer> vertexUniformBuffer = nil;

    for (MTLArgument* arg in refl.vertexArguments)
    {
        if (arg.type == MTLArgumentTypeBuffer && arg.bufferDataType == MTLDataTypeStruct)
        {
            vertexUniformBuffer = [mtl_s->device newBufferWithLength:256 options:MTLResourceStorageModeShared];
            break;
        }
    }

    LapizShader* shader = calloc(1, sizeof(LapizShader));

    if (!shader) 
        return NULL;

    shader->pipeline = pipeline;
    shader->uniformBuffer = uniformBuffer;
    shader->vertexUniformBuffer = vertexUniformBuffer;
    MetalBuildUniformHash(&shader->uniformHash, refl.fragmentArguments, &colorOffset);
    shader->colorOffset = colorOffset;
    MetalBuildUniformHash(&shader->vertexUniformHash, refl.vertexArguments, NULL);

    return shader;
}

LapizShader* LapizMTLShaderLoadFromMemory(const char* vsCode, const char* fsCode)
{
    if (!vsCode && !fsCode)
        return LapizMTLShaderLoadDefault();

    const char* source = vsCode ? vsCode : fsCode;
    return load_from_msl_source(source, NULL, NULL);
}

LapizShader* LapizMTLShaderLoadFromFile(const char* vertPath, const char* fragPath)
{
    return LapizMTLShaderLoadFromFileEx(vertPath, fragPath, NULL, NULL);
}

LapizShader* LapizMTLShaderLoadFromFileEx(const char* vertPath, const char* fragPath, const char* vertEntry, const char* fragEntry)
{
    const char* path = vertPath ? vertPath : fragPath;
    if (!path) 
        return NULL;

    g_metal_shader_error[0] = '\0';
    char* source = LapizLoadFileText(path);

    if (!source)
    {
        strncpy(g_metal_shader_error, "Failed to load shader file", LAPIZ_SHADER_ERROR_MAX - 1);
        g_metal_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        LapizSetError(&L_State.error, LAPIZ_ERROR_IO, "Failed to load shader file");
        LAPIZ_PRINT_ERROR(LAPIZ_ERROR_IO, "Failed to load shader file");
        return NULL;
    }

    LapizShader* shader = load_from_msl_source(source, vertEntry, fragEntry);
    free(source);
    return shader;
}

void LapizMTLShaderUnload(LapizShader* shader)
{
    if (!shader) 
        return;

    shader->pipeline = nil;
    shader->uniformBuffer = nil;
    shader->vertexUniformBuffer = nil;
    free(shader->uniformHash.entries);

    shader->uniformHash.entries = NULL;
    shader->uniformHash.count = 0;
    free(shader->vertexUniformHash.entries);

    shader->vertexUniformHash.entries = NULL;
    shader->vertexUniformHash.count = 0;
    free(shader);
}

int LapizMTLShaderIsValid(const LapizShader* shader)
{
    return (shader && shader->pipeline != nil) ? 1 : 0;
}

const char* LapizMTLShaderGetCompileError(void)
{
    return (g_metal_shader_error[0] != '\0') ? g_metal_shader_error : NULL;
}

int LapizMTLShaderGetLocation(const LapizShader* shader, const char* uniformName)
{
    if (!shader || !uniformName || !shader->uniformHash.entries) 
        return -1;

    return MetalHashLookup(&shader->uniformHash, uniformName);
}

int LapizMTLShaderGetVertexLocation(const LapizShader* shader, const char* uniformName)
{
    if (!shader || !uniformName || !shader->vertexUniformHash.entries) 
        return -1;

    return MetalHashLookup(&shader->vertexUniformHash, uniformName);
}

int LapizMTLShaderGetDefaultLocation(LapizShader* shader, LapizShaderLocationIndex idx)
{
    if (!shader) 
        return -1;
    switch (idx)
    {
        case LAPIZ_SHADER_LOC_COLOR: return (int)shader->colorOffset;
        case LAPIZ_SHADER_LOC_MVP:   return -1;
        case LAPIZ_SHADER_LOC_COUNT: return -1;
    }
    return -1;
}

void LapizMTLShaderUse(LapizShader* shader)
{
    mtl_s->current_shader = shader;

    if (!shader || !shader->pipeline) 
        return;
    if (!mtl_s || !mtl_s->enc) 
        return;

    [mtl_s->enc setRenderPipelineState:shader->pipeline];

    if (shader->vertexUniformBuffer)
        [mtl_s->enc setVertexBuffer:shader->vertexUniformBuffer offset:0 atIndex:0];

    [mtl_s->enc setFragmentBuffer:shader->uniformBuffer offset:0 atIndex:0];
}

void LapizMTLShaderSetFloat(LapizShader* shader, int loc, float value)
{
    if (!shader || !shader->uniformBuffer || loc < 0) 
        return;

    float* ptr = (float*)((char*)shader->uniformBuffer.contents + loc);
    *ptr = value;
}

void LapizMTLShaderSetVec2(LapizShader* shader, int loc, const float* v)
{
    if (!shader || !shader->uniformBuffer || loc < 0 || !v) 
        return;

    float* ptr = (float*)((char*)shader->uniformBuffer.contents + loc);
    ptr[0] = v[0]; ptr[1] = v[1];
}

void LapizMTLShaderSetVec3(LapizShader* shader, int loc, const float* v)
{
    if (!shader || !shader->uniformBuffer || loc < 0 || !v) 
        return;
    
    float* ptr = (float*)((char*)shader->uniformBuffer.contents + loc);
    ptr[0] = v[0]; ptr[1] = v[1]; ptr[2] = v[2];
}

void LapizMTLShaderSetVec4(LapizShader* shader, int loc, const float* v)
{
    if (!shader || !shader->uniformBuffer || loc < 0 || !v) 
        return;
    
    float* ptr = (float*)((char*)shader->uniformBuffer.contents + loc);
    ptr[0] = v[0]; ptr[1] = v[1]; ptr[2] = v[2]; ptr[3] = v[3];
}

void LapizMTLShaderSetInt(LapizShader* shader, int loc, int value)
{
    if (!shader || !shader->uniformBuffer || loc < 0) 
        return;
    
    int* ptr = (int*)((char*)shader->uniformBuffer.contents + loc);
    *ptr = value;
}

void LapizMTLShaderSetMatrix4(LapizShader* shader, int loc, const float* m)
{
    if (!shader || !shader->uniformBuffer || loc < 0 || !m) 
        return;

    memcpy((char*)shader->uniformBuffer.contents + loc, m, 16 * sizeof(float));
}

void LapizMTLShaderSetColor(LapizShader* shader, int loc, LapizColor color)
{
    LapizMTLShaderSetVec4(shader, loc, color);
}

void LapizMTLShaderSetTexture(LapizShader* shader, int loc, LapizTexture* texture)
{
    LapizMTLShaderSetTextureEx(shader, loc, texture, 0);
}

void LapizMTLShaderSetTextureEx(LapizShader* shader, int loc, LapizTexture* texture, int slot)
{
    (void)loc;
    if (!shader || !texture || !texture->_backend) 
        return;
    if (!mtl_s || !mtl_s->enc) 
        return;

    id<MTLTexture> mtlTex = (__bridge id<MTLTexture>)texture->_backend;
    [mtl_s->enc setFragmentTexture:mtlTex atIndex:(NSUInteger)slot];
}

#pragma clang diagnostic pop
