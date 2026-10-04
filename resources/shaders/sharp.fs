#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 sourceSize; // native texture size in texels (WIDE_W, NATIVE_H)
uniform vec2 outputScale; // real output pixels per source texel (destW/sourceSize.x, destH/sourceSize.y)

out vec4 finalColor;

// Classic "sharp bilinear" (RetroArch's sharp-bilinear-simple): keeps pixel
// edges crisp like nearest-neighbor, but still blends smoothly across each
// edge instead of aliasing, by biasing the sample point toward the nearest
// texel center except within a thin band around each texel boundary (whose
// width shrinks as outputScale grows, so it stays a fixed ~1 output pixel
// wide regardless of window size) where it lets hardware bilinear blend
// normally. Requires the texture's own sampler to be set to bilinear -
// this shader only reshapes *where* that sampling happens, it doesn't
// blend samples itself.
void main()
{
    vec2 texel = fragTexCoord * sourceSize;
    vec2 texel_floored = floor(texel);
    vec2 s = fract(texel);

    vec2 region_range = 0.5 - 0.5 / outputScale;
    vec2 center_dist = s - 0.5;
    vec2 f = (center_dist - clamp(center_dist, -region_range, region_range)) * outputScale + 0.5;

    vec2 mod_texel = texel_floored + f;
    vec4 texColor = texture(texture0, mod_texel / sourceSize);

    finalColor = texColor * fragColor * colDiffuse;
}
