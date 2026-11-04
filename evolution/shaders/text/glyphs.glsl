// Shared by the text pass shaders: one 8x8 workgroup per glyph instance, one invocation per font pixel of
// its cell. Each covered pixel of the target is visited by exactly one invocation of one workgroup.

layout(local_size_x = 8, local_size_y = 8) in;

// Must match TextPass::GlyphInstance in src/text/text_pass.hpp (16 bytes).
struct GlyphInstance {
    int x;          // the top-left pixel of the glyph cell in the target
    int y;
    uint codeScale; // glyph code (32..127) in the low byte, the integer scale above it
    uint rgba;      // red in the low byte
};

// The R8_UNORM font atlas: 16 glyphs per row, code 32 in the top-left cell.
layout(set = 0, binding = 0) uniform sampler2D atlas;
layout(set = 0, binding = 2, std430) readonly buffer Instances {
    GlyphInstance glyphs[];
} instances;

// Must match TextPushConstants in src/text/text_pass.cpp (4 bytes).
layout(push_constant) uniform PushConstants {
    uint first; // the instance of workgroup 0
} pc;

GlyphInstance currentGlyph() {
    return instances.glyphs[pc.first + gl_WorkGroupID.x];
}

int glyphScale(GlyphInstance glyph) {
    return int(glyph.codeScale >> 8);
}

// Whether this invocation's pixel of the glyph is set in the font.
bool fontPixelSet(GlyphInstance glyph) {
    uint index = (glyph.codeScale & 0xffu) - 32u;
    ivec2 texel = ivec2(index % 16u, index / 16u) * 8 + ivec2(gl_LocalInvocationID.xy);
    return texelFetch(atlas, texel, 0).r > 0.5;
}

// The top-left target pixel of this invocation's scale x scale block.
ivec2 blockOrigin(GlyphInstance glyph) {
    return ivec2(glyph.x, glyph.y) + ivec2(gl_LocalInvocationID.xy) * glyphScale(glyph);
}
