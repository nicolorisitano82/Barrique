#version 450
// Barrique stress3d: Vulkan vertex shader (same work as the HLSL one in stress3d.c)
layout(push_constant) uniform pc { mat4 vp; } u;   // row-vector matrices, see stress3d.c
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nrm;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 m0;
layout(location = 4) in vec4 m1;
layout(location = 5) in vec4 m2;
layout(location = 6) in vec4 m3;
layout(location = 7) in vec4 col;
layout(location = 0) out vec4 out_col;
layout(location = 1) out vec2 out_uv;
void main()
{
    mat4 model = mat4( m0, m1, m2, m3 );          // rows of the row-vector matrix as columns
    vec4 wp = vec4( pos, 1.0 ) * transpose( model );
    vec3 wn = normalize( ( vec4( nrm, 0.0 ) * transpose( model ) ).xyz );
    float d = max( dot( wn, vec3( 0.408248, 0.408248, -0.816497 ) ), 0.0 );
    gl_Position = wp * transpose( u.vp );
    out_col = vec4( col.rgb * ( 0.25 + 0.75 * d ), 1.0 );
    out_uv = uv;
}
