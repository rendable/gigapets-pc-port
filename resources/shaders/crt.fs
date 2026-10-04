#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 outputSize; // actual on-screen draw size in pixels (destW, destH)

out vec4 finalColor;

void main()
{
    vec4 texColor = texture(texture0, fragTexCoord);

    // Scanlines synced to real output pixel rows, not texel rows, so they
    // stay stable regardless of window size / scale factor. Uses row
    // parity (floor+mod) rather than a sine wave - a sine sampled exactly
    // at pixel-row centers with a 2-row period always lands on a peak or
    // trough (sin(N+0.5)*pi) = +-1 for every row), so scan*scan was always
    // ~1 and the scanline term was a silent no-op; only the vignette below
    // was ever visible.
    float rowIndex = floor(fragTexCoord.y * outputSize.y);
    float isOddRow = mod(rowIndex, 2.0);
    float scanlineFactor = mix(1.0, 0.72, isOddRow);
    texColor.rgb *= scanlineFactor;

    // Soft vignette darkening toward the edges.
    vec2 centered = fragTexCoord - 0.5;
    float vignette = 1.0 - dot(centered, centered) * 0.55;
    texColor.rgb *= vignette;

    finalColor = texColor * fragColor * colDiffuse;
}
