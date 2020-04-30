cbuffer u_cameraPlaneParams
{
  float s_CameraNearPlane;
  float s_CameraFarPlane;
  float u_clipZNear;
  float u_clipZFar;
};

struct PS_INPUT
{
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
  float3 normal : NORMAL;
  float4 colour : COLOR0;
  float2 fLogDepth : TEXCOORD1;
  float2 objectInfo : TEXCOORD2;
};

struct PS_OUTPUT
{
  float4 Color0 : SV_Target0;
  float4 Normal : SV_Target1;
  float Depth0 : SV_Depth;
};

sampler albedoSampler;
Texture2D albedoTexture;

PS_OUTPUT main(PS_INPUT input)
{
  PS_OUTPUT output;
  float4 col = albedoTexture.Sample(albedoSampler, input.uv);
  output.Color0 = col * input.colour;

  float halfFcoef = 1.0 / log2(s_CameraFarPlane + 1.0);
  output.Depth0 = log2(input.fLogDepth.x) * halfFcoef;

  output.Normal = float4(0.0, 0.0, input.objectInfo.x, output.Depth0); // packed 
  return output;
}