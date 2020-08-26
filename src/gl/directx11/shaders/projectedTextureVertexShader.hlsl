cbuffer u_cameraPlaneParams
{
  float s_CameraNearPlane;
  float s_CameraFarPlane;
  float u_clipZNear;
  float u_clipZFar;
};

struct VS_INPUT
{
  float3 pos : POSITION;
  float2 uv : TEXCOORD0;
};

struct PS_INPUT
{
  float4 pos : SV_POSITION;
  float4 clip : TEXCOORD0;
  float2 depthInfo : TEXCOORD1;
  float2 uv : TEXCOORD2;
};

cbuffer u_vertParams : register(b0)
{
  float4x4 u_worldViewProjectionMatrix;
};

float2 PackDepth(float4 clipPos)
{
  if (u_worldViewProjectionMatrix[3][1] == 0.0) // orthographic
    return float2(clipPos.z, clipPos.w);

  // perspective
  return float2(1.0 + clipPos.w, 0);
}

PS_INPUT main(VS_INPUT input)
{
  PS_INPUT output;

  output.pos = mul(u_worldViewProjectionMatrix, float4(input.pos, 1.0));
  output.depthInfo = PackDepth(output.pos);
  output.clip = output.pos;
  output.uv = input.uv;
  
  return output;
}
