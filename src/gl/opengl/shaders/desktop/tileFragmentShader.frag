#version 330
#extension GL_ARB_separate_shader_objects : require

layout(std140) uniform type_u_cameraPlaneParams
{
    float s_CameraNearPlane;
    float s_CameraFarPlane;
    float u_clipZNear;
    float u_clipZFar;
} u_cameraPlaneParams;

uniform sampler2D SPIRV_Cross_CombinedcolourTexturecolourSampler;

layout(location = 0) in vec4 in_var_COLOR0;
layout(location = 1) in vec2 in_var_TEXCOORD0;
layout(location = 2) in vec2 in_var_TEXCOORD1;
layout(location = 0) out vec4 out_var_SV_Target;

float _32;

void main()
{
    vec4 _60 = vec4(texture(SPIRV_Cross_CombinedcolourTexturecolourSampler, in_var_TEXCOORD0).xyz * in_var_COLOR0.xyz, _32);
    _60.w = ((in_var_TEXCOORD1.x / in_var_TEXCOORD1.y) * (1.0 / (u_cameraPlaneParams.u_clipZFar - u_cameraPlaneParams.u_clipZNear))) + (u_cameraPlaneParams.u_clipZNear * (-0.5));
    out_var_SV_Target = _60;
}

