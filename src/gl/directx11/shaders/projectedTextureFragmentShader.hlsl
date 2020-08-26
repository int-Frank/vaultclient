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
  float4 clip : TEXCOORD0;
  float2 depthInfo : TEXCOORD1;
  float2 uv : TEXCOORD2;
};

struct PS_OUTPUT
{
  float4 Color0 : SV_Target0;
  float Depth0 : SV_Depth;
};

sampler textureSampler;
Texture2D textureTexture;

sampler sceneDepthSampler;
Texture2D sceneDepthTexture;

cbuffer u_fragParams : register(b0)
{
  float4x4 u_inverseModelViewProjection;
  float4x4 u_inverseProjection;
  float4 u_eyeBoundsA;
  float4 u_eyeBoundsB;
  float4 u_eyeBoundsC;
  float4 u_eyeBoundsD;
};

float logToLinearDepth(float logDepth)
{
  float a = s_CameraFarPlane / (s_CameraFarPlane - s_CameraNearPlane);
  float b = s_CameraFarPlane * s_CameraNearPlane / (s_CameraNearPlane - s_CameraFarPlane);
  float worldDepth = pow(2.0, logDepth * log2(s_CameraFarPlane + 1.0)) - 1.0;
  return (a + b / worldDepth);
}

float linearDepthToClipZ(float depth)
{
  return depth * (u_clipZFar - u_clipZNear) + u_clipZNear;
}

float CalculateDepth(float2 depthInfo)
{
  if (depthInfo.y != 0.0) // orthographic
    return (depthInfo.x / depthInfo.y);
	
  // log-z (perspective)
  float halfFcoef = 1.0 / log2(s_CameraFarPlane + 1.0);
  return log2(depthInfo.x) * halfFcoef;
}

PS_OUTPUT main(PS_INPUT input)
{
  PS_OUTPUT output;

  float2 screenUV = (input.clip.xy / input.clip.w) * 0.5 + float2(0.5, 0.5);
  
  float logDepth = sceneDepthTexture.Sample(sceneDepthSampler, float2(screenUV.x, 1.0 - screenUV.y)).x;
  float depth = logToLinearDepth(logDepth);
  float clipZ = linearDepthToClipZ(depth);

  float4 fragEyePosition = mul(u_inverseProjection, float4(screenUV * 2 - 1, clipZ, 1.0));
  fragEyePosition /= fragEyePosition.w;
  
  float4 localPosition = mul(u_inverseModelViewProjection, float4(screenUV * 2 - 1, clipZ, 1.0));
  localPosition /= localPosition.w;
  
  float2 uv = input.uv;//localPosition.xy * 0.5 + 0.5;
  float4 col = textureTexture.Sample(textureSampler, float2(uv.x, uv.y)); // flip here, but i actually think the texture is flipped
  
  //float4 fragEyePosition = mul(u_inverseProjection, float4(input.clip.xy, clipZ, 1.0));
  //fragEyePosition /= fragEyePosition.w;

  //if (localPosition.x < -1 || localPosition.x > 1 || localPosition.y < -1 || localPosition.y > 1 || localPosition.z < -1 || localPosition.z > 1)
  //  output.Color0 = float4(0,0,0,0);
  //else
    output.Color0 = float4(col.xyz, 0.75);//length(col.xyz) * 2.0 * u_eyeBoundsA.x);
	
  //if (length(col.xyz) <= 0.0)
  //  output.Color0.w = 0.0;

  //float distRange = length(u_eyeBoundsA - u_eyeBoundsD);
  ////float distHor = length(u_eyeBoundsA - u_eyeBoundsD);
  //
  //float distA = length(fragEyePosition.xyz - u_eyeBoundsA);
  //float distB = length(fragEyePosition.xyz - u_eyeBoundsB);
  //float distC = length(fragEyePosition.xyz - u_eyeBoundsC);
  //float distD = length(fragEyePosition.xyz - u_eyeBoundsD);
  //
  //float3 finalUV = 
  
  //if (fragEyePosition.x < -u_eyeBoundsA.x)
  //  output.Color0 = float4(0,0,0,0);

 // output.Color0 = float4(fragEyePosition.xyz,0.99) + col * 0.0001;

  output.Depth0 = CalculateDepth(input.depthInfo.xy);
  return output;
}
