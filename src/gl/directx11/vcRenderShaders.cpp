#include "gl/vcRenderShaders.h"
#include "udPlatformUtil.h"

#define FRAG_HEADER "cbuffer u_cameraPlaneParams { float s_CameraNearPlane; float s_CameraFarPlane; float u_unused1; float u_unused2; };"
#define VERT_HEADER "cbuffer u_cameraPlaneParams { float s_CameraNearPlane; float s_CameraFarPlane; float u_unused1; float u_unused2; };"

const char *const g_PolygonP3N3UV2FragmentShader = FRAG_HEADER R"shader(
  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float4 colour : COLOR0;
    float2 fLogDepth : TEXCOORD1;
  };

  struct PS_OUTPUT
  {
    float4 Color0 : SV_Target;
    float Depth0 : SV_Depth;
  };

  sampler sampler0;
  Texture2D texture0;

  PS_OUTPUT main(PS_INPUT input)
  {
    PS_OUTPUT output;
    float4 col = texture0.Sample(sampler0, input.uv);
    float4 diffuseColour = col * input.colour;

    // some fixed lighting
    float3 lightDirection = normalize(float3(0.85, 0.15, 0.5));
    float ndotl = dot(input.normal, lightDirection) * 0.5 + 0.5;
    float3 diffuse = diffuseColour.xyz * ndotl;

    output.Color0 = float4(diffuse, diffuseColour.a);

    float halfFcoef  = 1.0 / log2(s_CameraFarPlane + 1.0);
    output.Depth0 = log2(input.fLogDepth.x) * halfFcoef;

    return output;
  }
)shader";

const char *const g_PolygonP3N3UV2VertexShader = VERT_HEADER R"shader(
  struct VS_INPUT
  {
    float3 pos : POSITION;
    float2 uv  : TEXCOORD0;
    float3 normal : NORMAL;
    //float4 colour : COLOR0;
  };

  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float3 normal : NORMAL;
    float4 colour : COLOR0;
    float2 fLogDepth : TEXCOORD1;
  };

  cbuffer u_EveryObject : register(b0)
  {
    float4x4 u_worldViewProjectionMatrix;
    float4x4 u_worldMatrix;
    float4 u_colour;
  };

  PS_INPUT main(VS_INPUT input)
  {
    PS_INPUT output;

    // making the assumption that the model matrix won't contain non-uniform scale
    float3 worldNormal = normalize(mul(u_worldMatrix, float4(input.normal, 0.0)).xyz);

    output.pos = mul(u_worldViewProjectionMatrix, float4(input.pos, 1.0));
    output.uv = input.uv;
    output.normal = worldNormal;
    output.colour = u_colour;// * input.colour;
    output.fLogDepth.x = 1.0 + output.pos.w;

    return output;
  }
)shader";

const char *const g_ImageRendererFragmentShader = FRAG_HEADER R"shader(
  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 colour : COLOR0;
    float2 fLogDepth : TEXCOORD1;
  };

  sampler sampler0;
  Texture2D texture0;

  struct PS_OUTPUT
  {
    float4 Color0 : SV_Target;
    float Depth0 : SV_Depth;
  };

  PS_OUTPUT main(PS_INPUT input)
  {
    PS_OUTPUT output;
    float4 col = texture0.Sample(sampler0, input.uv);
    output.Color0 = col * input.colour;

    float halfFcoef = 1.0 / log2(s_CameraFarPlane + 1.0);
    output.Depth0 = log2(input.fLogDepth.x) * halfFcoef;

    return output;
  }
)shader";

const char *const g_ImageRendererMeshVertexShader = VERT_HEADER R"shader(
  struct VS_INPUT
  {
    float3 pos : POSITION;
    float3 normal : NORMAL; // unused
    float2 uv  : TEXCOORD0;
  };

  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 colour : COLOR0;
    float2 fLogDepth : TEXCOORD1;
  };

  cbuffer u_EveryObject : register(b0)
  {
    float4x4 u_worldViewProjectionMatrix;
    float4 u_colour;
    float4 u_screenSize; // unused
  };

  PS_INPUT main(VS_INPUT input)
  {
    PS_INPUT output;

    output.pos = mul(u_worldViewProjectionMatrix, float4(input.pos, 1.0));
    output.uv = input.uv;
    output.colour = u_colour;
    output.fLogDepth.x = 1.0 + output.pos.w;

    return output;
  }
)shader";

const char *const g_ImageRendererBillboardVertexShader = VERT_HEADER R"shader(
  struct VS_INPUT
  {
    float3 pos : POSITION;
    float2 uv  : TEXCOORD0;
  };

  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 colour : COLOR0;
    float2 fLogDepth : TEXCOORD1;
  };

  cbuffer u_EveryObject : register(b0)
  {
    float4x4 u_worldViewProjectionMatrix;
    float4 u_colour;
    float4 u_screenSize;
  };

  PS_INPUT main(VS_INPUT input)
  {
    PS_INPUT output;

    output.pos = mul(u_worldViewProjectionMatrix, float4(input.pos, 1.0));
    output.pos.xy += u_screenSize.z * output.pos.w * u_screenSize.xy * float2(input.uv.x * 2.0 - 1.0, input.uv.y * 2.0 - 1.0); // expand billboard

    output.uv = float2(input.uv.x, 1.0 - input.uv.y);
    output.colour = u_colour;
    output.fLogDepth.x = 1.0 + output.pos.w;

    return output;
  }
)shader";

const char *const g_FlatColour_FragmentShader = FRAG_HEADER R"shader(
  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float3 normal : NORMAL;
    float4 colour : COLOR0;
    float2 fLogDepth : TEXCOORD1;
  };

  struct PS_OUTPUT
  {
    float4 Color0 : SV_Target;
    float Depth0 : SV_Depth;
  };

  PS_OUTPUT main(PS_INPUT input)
  {
    PS_OUTPUT output;

    output.Color0 = input.colour;

    float halfFcoef = 1.0 / log2(s_CameraFarPlane + 1.0);
    output.Depth0 = log2(input.fLogDepth.x) * halfFcoef;

    return output;
  }
)shader";

const char *const g_DepthOnly_FragmentShader = FRAG_HEADER R"shader(
  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float4 colour : COLOR0;
    float2 fLogDepth : TEXCOORD1;
  };

  struct PS_OUTPUT
  {
    float4 Color0 : SV_Target;
    float Depth0 : SV_Depth;
  };

  PS_OUTPUT main(PS_INPUT input)
  {
    PS_OUTPUT output;

    output.Color0 = float4(0.0, 0.0, 0.0, 0.0);

    float halfFcoef = 1.0 / log2(s_CameraFarPlane + 1.0);
    output.Depth0 = log2(input.fLogDepth.x) * halfFcoef;

    return output;
  }
)shader";

const char *const g_BlurVertexShader = VERT_HEADER R"shader(
  struct VS_INPUT
  {
    float3 pos : POSITION;
    float2 uv  : TEXCOORD0;
  };

  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv0  : TEXCOORD0;
    float2 uv1  : TEXCOORD1;
    float2 uv2  : TEXCOORD2;
  };

  cbuffer u_EveryFrame : register(b0)
  {
    float4 u_stepSize; // remember: requires 16 byte alignment
  };

  PS_INPUT main(VS_INPUT input)
  {
    PS_INPUT output;

    output.pos = float4(input.pos.x, input.pos.y, 0.0, 1.0);

    // sample on edges, taking advantage of bilinear sampling
    float2 sampleOffset = 1.42 * u_stepSize.xy;
    float2 uv = float2(input.uv.x, 1.0 - input.uv.y);
    output.uv0 = uv - sampleOffset;
    output.uv1 = uv;
    output.uv2 = uv + sampleOffset;

    return output;
  }
)shader";

const char *const g_BlurFragmentShader = FRAG_HEADER R"shader(
  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv0  : TEXCOORD0;
    float2 uv1  : TEXCOORD1;
    float2 uv2  : TEXCOORD2;
  };

  sampler sampler0;
  Texture2D texture0;

  static float4 kernel[3] = {float4(0.0, 0.0, 0.0, 0.27901),
                              float4(1.0, 1.0, 1.0, 0.44198),
                              float4(0.0, 0.0, 0.0, 0.27901)};

  float4 main(PS_INPUT input) : SV_Target
  {
    float4 colour = float4(0.0, 0.0, 0.0, 0.0);

    colour += kernel[0] * texture0.Sample(sampler0, input.uv0);
    colour += kernel[1] * texture0.Sample(sampler0, input.uv1);
    colour += kernel[2] * texture0.Sample(sampler0, input.uv2);

    return colour;
  }

)shader";

const char *const g_HighlightVertexShader = VERT_HEADER R"shader(
  struct VS_INPUT
  {
    float3 pos : POSITION;
    float2 uv  : TEXCOORD0;
  };

  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv0  : TEXCOORD0;
    float2 uv1  : TEXCOORD1;
    float2 uv2  : TEXCOORD2;
    float2 uv3  : TEXCOORD3;
    float2 uv4  : TEXCOORD4;
    float4 colour : COLOR0;
    float4 stepSizeThickness : COLOR1;
  };

  static float2 searchKernel[4] = {float2(-1, -1), float2(1, -1), float2(-1,  1), float2(1,  1)};

  cbuffer u_EveryFrame : register(b0)
  {
    float4 u_stepSizeThickness; // (stepSize.xy, outline thickness, inner overlay strength)
    float4 u_colour;
  };

  PS_INPUT main(VS_INPUT input)
  {
    PS_INPUT output;

    output.pos = float4(input.pos.x, input.pos.y, 0.0, 1.0);
    output.colour = u_colour;
    output.stepSizeThickness = u_stepSizeThickness;

    output.uv0 = input.uv;
    output.uv1 = input.uv + u_stepSizeThickness.xy * searchKernel[0];
    output.uv2 = input.uv + u_stepSizeThickness.xy * searchKernel[1];
    output.uv3 = input.uv + u_stepSizeThickness.xy * searchKernel[2];
    output.uv4 = input.uv + u_stepSizeThickness.xy * searchKernel[3];

    return output;
  }
)shader";

const char *const g_HighlightFragmentShader = FRAG_HEADER R"shader(
  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv0  : TEXCOORD0;
    float2 uv1  : TEXCOORD1;
    float2 uv2  : TEXCOORD2;
    float2 uv3  : TEXCOORD3;
    float2 uv4  : TEXCOORD4;
    float4 colour : COLOR0;
    float4 stepSizeThickness : COLOR1;
  };

  sampler sampler0;
  Texture2D u_texture;

  float4 main(PS_INPUT input) : SV_Target
  {
    float4 middle = u_texture.Sample(sampler0, input.uv0);
    float result = middle.w;

    // 'outside' the geometry, just use the blurred 'distance'
    if (middle.x == 0.0)
      return float4(input.colour.xyz, result * input.stepSizeThickness.z * input.colour.a);

    result = 1.0 - result;

    // look for an edge, setting to full colour if found
    float softenEdge = 0.15 * input.colour.a;
    result += softenEdge * step(u_texture.Sample(sampler0, input.uv1).x - middle.x, -0.00001);
    result += softenEdge * step(u_texture.Sample(sampler0, input.uv2).x - middle.x, -0.00001);
    result += softenEdge * step(u_texture.Sample(sampler0, input.uv3).x - middle.x, -0.00001);
    result += softenEdge * step(u_texture.Sample(sampler0, input.uv4).x - middle.x, -0.00001);

    result = max(input.stepSizeThickness.w, result) * input.colour.w; // overlay colour
    return float4(input.colour.xyz, result);
  }

)shader";


const char *const g_udGPURenderQuadVertexShader = VERT_HEADER R"shader(
  struct VS_INPUT
  {
    float4 pos : POSITION;
    float4 colour  : COLOR0;
    float2 corner: TEXCOORD0;
  };

  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float4 colour : COLOR0;
  };

  cbuffer u_EveryObject : register(b0)
  {
    float4x4 u_worldViewProjectionMatrix;
  };

  PS_INPUT main(VS_INPUT input)
  {
    PS_INPUT output;

    output.colour = input.colour.bgra;

    // Points
    float4 off = float4(input.pos.www * 2.0, 0);
    float4 pos0 = mul(u_worldViewProjectionMatrix, float4(input.pos.xyz + off.www, 1.0));
    float4 pos1 = mul(u_worldViewProjectionMatrix, float4(input.pos.xyz + off.xww, 1.0));
    float4 pos2 = mul(u_worldViewProjectionMatrix, float4(input.pos.xyz + off.xyw, 1.0));
    float4 pos3 = mul(u_worldViewProjectionMatrix, float4(input.pos.xyz + off.wyw, 1.0));
    float4 pos4 = mul(u_worldViewProjectionMatrix, float4(input.pos.xyz + off.wwz, 1.0));
    float4 pos5 = mul(u_worldViewProjectionMatrix, float4(input.pos.xyz + off.xwz, 1.0));
    float4 pos6 = mul(u_worldViewProjectionMatrix, float4(input.pos.xyz + off.xyz, 1.0));
    float4 pos7 = mul(u_worldViewProjectionMatrix, float4(input.pos.xyz + off.wyz, 1.0));

    float4 minPos, maxPos;
    minPos = min(pos0, pos1);
    minPos = min(minPos, pos2);
    minPos = min(minPos, pos3);
    minPos = min(minPos, pos4);
    minPos = min(minPos, pos5);
    minPos = min(minPos, pos6);
    minPos = min(minPos, pos7);
    maxPos = max(pos0, pos1);
    maxPos = max(maxPos, pos2);
    maxPos = max(maxPos, pos3);
    maxPos = max(maxPos, pos4);
    maxPos = max(maxPos, pos5);
    maxPos = max(maxPos, pos6);
    maxPos = max(maxPos, pos7);
    output.pos = minPos + (maxPos - minPos) * 0.5;

    float2 pointSize = float2(maxPos.x - minPos.x, maxPos.y - minPos.y);
    output.pos.xy += pointSize * input.corner * float2(0.5, 0.5);
    return output;
  }
)shader";

const char *const g_udGPURenderQuadFragmentShader = FRAG_HEADER R"shader(
  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float4 colour : COLOR0;
  };

  float4 main(PS_INPUT input) : SV_Target
  {
    return input.colour;
  }
)shader";

const char *const g_PostEffectsVertexShader = VERT_HEADER R"shader(
  struct VS_INPUT
  {
    float3 pos : POSITION;
    float2 uv  : TEXCOORD0;
  };

  struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 edgeSampleUV0 : TEXCOORD1;
    float2 edgeSampleUV1 : TEXCOORD2;
    float2 edgeSampleUV2 : TEXCOORD3;
    float2 edgeSampleUV3 : TEXCOORD4;
    float2 sampleStepSize : TEXCOORD5;
    float saturation : TEXCOORD6;
  };

  cbuffer u_params : register(b0)
  {
    float4 u_screenParams;  // sampleStepSizex, sampleStepSizeY, (unused), (unused)
    float4 u_saturation; // saturation, (unused), (unused), (unused)
  }

  PS_INPUT main(VS_INPUT input)
  {
    PS_INPUT output;
    output.pos = float4(input.pos.xy, 0.f, 1.f);
    output.uv  = input.uv;
    output.sampleStepSize = u_screenParams.xy;

    // sample corners
    output.edgeSampleUV0 = output.uv + u_screenParams.xy;
    output.edgeSampleUV1 = output.uv - u_screenParams.xy;
    output.edgeSampleUV2 = output.uv + float2(u_screenParams.x, -u_screenParams.y);
    output.edgeSampleUV3 = output.uv + float2(-u_screenParams.x, u_screenParams.y);

    output.saturation = u_saturation.x;

    return output;
  }
)shader";

const char *const g_PostEffectsFragmentShader = FRAG_HEADER R"shader(

/*
============================================================================
                    NVIDIA FXAA 3.11 by TIMOTHY LOTTES
------------------------------------------------------------------------------
COPYRIGHT (C) 2010, 2011 NVIDIA CORPORATION. ALL RIGHTS RESERVED.
------------------------------------------------------------------------------
TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE IS PROVIDED
*AS IS* AND NVIDIA AND ITS SUPPLIERS DISCLAIM ALL WARRANTIES, EITHER EXPRESS
OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL NVIDIA
OR ITS SUPPLIERS BE LIABLE FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR
CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR
LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION,
OR ANY OTHER PECUNIARY LOSS) ARISING OUT OF THE USE OF OR INABILITY TO USE
THIS SOFTWARE, EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
DAMAGES.
*/

#define FXAA_GREEN_AS_LUMA 1
#define FXAA_DISCARD 0
#define FXAA_FAST_PIXEL_OFFSET 0
#define FXAA_GATHER4_ALPHA 0

#define FXAA_QUALITY__PS 5
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 4.0
#define FXAA_QUALITY__P4 12.0

#define FxaaBool bool
#define FxaaDiscard clip(-1)
#define FxaaFloat float
#define FxaaFloat2 float2
#define FxaaFloat3 float3
#define FxaaFloat4 float4
#define FxaaHalf half
#define FxaaHalf2 half2
#define FxaaHalf3 half3
#define FxaaHalf4 half4
#define FxaaSat(x) saturate(x)

#define FxaaInt2 int2
struct FxaaTex { SamplerState smpl; Texture2D tex; };
#define FxaaTexTop(t, p) t.tex.SampleLevel(t.smpl, p, 0.0)
#define FxaaTexOff(t, p, o, r) t.tex.SampleLevel(t.smpl, p, 0.0, o)
#define FxaaTexAlpha4(t, p) t.tex.GatherAlpha(t.smpl, p)
#define FxaaTexOffAlpha4(t, p, o) t.tex.GatherAlpha(t.smpl, p, o)
#define FxaaTexGreen4(t, p) t.tex.GatherGreen(t.smpl, p)
#define FxaaTexOffGreen4(t, p, o) t.tex.GatherGreen(t.smpl, p, o)

FxaaFloat FxaaLuma(FxaaFloat4 rgba) { return rgba.y; }  

FxaaFloat4 FxaaPixelShader(
    FxaaFloat2 pos,
    FxaaFloat4 fxaaConsolePosPos,
    FxaaTex tex,
    FxaaTex fxaaConsole360TexExpBiasNegOne,
    FxaaTex fxaaConsole360TexExpBiasNegTwo,
    FxaaFloat2 fxaaQualityRcpFrame,
    FxaaFloat4 fxaaConsoleRcpFrameOpt,
    FxaaFloat4 fxaaConsoleRcpFrameOpt2,
    FxaaFloat4 fxaaConsole360RcpFrameOpt2,
    FxaaFloat fxaaQualitySubpix,
    FxaaFloat fxaaQualityEdgeThreshold,
    FxaaFloat fxaaQualityEdgeThresholdMin,
    FxaaFloat fxaaConsoleEdgeSharpness,
    FxaaFloat fxaaConsoleEdgeThreshold,
    FxaaFloat4 fxaaConsole360ConstDir
) {
    FxaaFloat2 posM;
    posM.x = pos.x;
    posM.y = pos.y;

    FxaaFloat4 rgbyM = FxaaTexTop(tex, posM);

    #define lumaM rgbyM.y

    FxaaFloat lumaS = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2( 0, 1), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaE = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2( 1, 0), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaN = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2( 0,-1), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaW = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(-1, 0), fxaaQualityRcpFrame.xy));

    FxaaFloat maxSM = max(lumaS, lumaM);
    FxaaFloat minSM = min(lumaS, lumaM);
    FxaaFloat maxESM = max(lumaE, maxSM);
    FxaaFloat minESM = min(lumaE, minSM);
    FxaaFloat maxWN = max(lumaN, lumaW);
    FxaaFloat minWN = min(lumaN, lumaW);
    FxaaFloat rangeMax = max(maxWN, maxESM);
    FxaaFloat rangeMin = min(minWN, minESM);
    FxaaFloat rangeMaxScaled = rangeMax * fxaaQualityEdgeThreshold;
    FxaaFloat range = rangeMax - rangeMin;
    FxaaFloat rangeMaxClamped = max(fxaaQualityEdgeThresholdMin, rangeMaxScaled);
    FxaaBool earlyExit = range < rangeMaxClamped;
    if(earlyExit)
        #if (FXAA_DISCARD == 1)
            FxaaDiscard;
        #else
            return rgbyM;
        #endif

    FxaaFloat lumaNW = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(-1,-1), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaSE = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2( 1, 1), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaNE = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2( 1,-1), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaSW = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(-1, 1), fxaaQualityRcpFrame.xy));

    FxaaFloat lumaNS = lumaN + lumaS;
    FxaaFloat lumaWE = lumaW + lumaE;
    FxaaFloat subpixRcpRange = 1.0/range;
    FxaaFloat subpixNSWE = lumaNS + lumaWE;
    FxaaFloat edgeHorz1 = (-2.0 * lumaM) + lumaNS;
    FxaaFloat edgeVert1 = (-2.0 * lumaM) + lumaWE;
    FxaaFloat lumaNESE = lumaNE + lumaSE;
    FxaaFloat lumaNWNE = lumaNW + lumaNE;
    FxaaFloat edgeHorz2 = (-2.0 * lumaE) + lumaNESE;
    FxaaFloat edgeVert2 = (-2.0 * lumaN) + lumaNWNE;
    FxaaFloat lumaNWSW = lumaNW + lumaSW;
    FxaaFloat lumaSWSE = lumaSW + lumaSE;
    FxaaFloat edgeHorz4 = (abs(edgeHorz1) * 2.0) + abs(edgeHorz2);
    FxaaFloat edgeVert4 = (abs(edgeVert1) * 2.0) + abs(edgeVert2);
    FxaaFloat edgeHorz3 = (-2.0 * lumaW) + lumaNWSW;
    FxaaFloat edgeVert3 = (-2.0 * lumaS) + lumaSWSE;
    FxaaFloat edgeHorz = abs(edgeHorz3) + edgeHorz4;
    FxaaFloat edgeVert = abs(edgeVert3) + edgeVert4;
    FxaaFloat subpixNWSWNESE = lumaNWSW + lumaNESE;
    FxaaFloat lengthSign = fxaaQualityRcpFrame.x;
    FxaaBool horzSpan = edgeHorz >= edgeVert;
    FxaaFloat subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;
    if(!horzSpan) lumaN = lumaW;
    if(!horzSpan) lumaS = lumaE;
    if(horzSpan) lengthSign = fxaaQualityRcpFrame.y;
    FxaaFloat subpixB = (subpixA * (1.0/12.0)) - lumaM;
    FxaaFloat gradientN = lumaN - lumaM;
    FxaaFloat gradientS = lumaS - lumaM;
    FxaaFloat lumaNN = lumaN + lumaM;
    FxaaFloat lumaSS = lumaS + lumaM;
    FxaaBool pairN = abs(gradientN) >= abs(gradientS);
    FxaaFloat gradient = max(abs(gradientN), abs(gradientS));
    if(pairN) lengthSign = -lengthSign;
    FxaaFloat subpixC = FxaaSat(abs(subpixB) * subpixRcpRange);
    FxaaFloat2 posB;
    posB.x = posM.x;
    posB.y = posM.y;
    FxaaFloat2 offNP;
    offNP.x = (!horzSpan) ? 0.0 : fxaaQualityRcpFrame.x;
    offNP.y = ( horzSpan) ? 0.0 : fxaaQualityRcpFrame.y;
    if(!horzSpan) posB.x += lengthSign * 0.5;
    if( horzSpan) posB.y += lengthSign * 0.5;
    FxaaFloat2 posN;
    posN.x = posB.x - offNP.x * FXAA_QUALITY__P0;
    posN.y = posB.y - offNP.y * FXAA_QUALITY__P0;
    FxaaFloat2 posP;
    posP.x = posB.x + offNP.x * FXAA_QUALITY__P0;
    posP.y = posB.y + offNP.y * FXAA_QUALITY__P0;
    FxaaFloat subpixD = ((-2.0)*subpixC) + 3.0;
    FxaaFloat lumaEndN = FxaaLuma(FxaaTexTop(tex, posN));
    FxaaFloat subpixE = subpixC * subpixC;
    FxaaFloat lumaEndP = FxaaLuma(FxaaTexTop(tex, posP));
    if(!pairN) lumaNN = lumaSS;
    FxaaFloat gradientScaled = gradient * 1.0/4.0;
    FxaaFloat lumaMM = lumaM - lumaNN * 0.5;
    FxaaFloat subpixF = subpixD * subpixE;
    FxaaBool lumaMLTZero = lumaMM < 0.0;
    lumaEndN -= lumaNN * 0.5;
    lumaEndP -= lumaNN * 0.5;
    FxaaBool doneN = abs(lumaEndN) >= gradientScaled;
    FxaaBool doneP = abs(lumaEndP) >= gradientScaled;
    if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P1;
    if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P1;
    FxaaBool doneNP = (!doneN) || (!doneP);
    if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P1;
    if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P1;
    if(doneNP) {
        if(!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
        if(!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
        if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
        if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P2;
        if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P2;
        doneNP = (!doneN) || (!doneP);
        if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P2;
        if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P2;
        if(doneNP) {
            if(!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
            if(!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
            if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
            if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
            doneN = abs(lumaEndN) >= gradientScaled;
            doneP = abs(lumaEndP) >= gradientScaled;
            if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P3;
            if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P3;
            doneNP = (!doneN) || (!doneP);
            if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P3;
            if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P3;
            if(doneNP) {
                if(!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                if(!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                doneN = abs(lumaEndN) >= gradientScaled;
                doneP = abs(lumaEndP) >= gradientScaled;
                if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P4;
                if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P4;
                doneNP = (!doneN) || (!doneP);
                if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P4;
                if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P4;
             
            }
        }
    }
    FxaaFloat dstN = posM.x - posN.x;
    FxaaFloat dstP = posP.x - posM.x;
    if(!horzSpan) dstN = posM.y - posN.y;
    if(!horzSpan) dstP = posP.y - posM.y;
    FxaaBool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero;
    FxaaFloat spanLength = (dstP + dstN);
    FxaaBool goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;
    FxaaFloat spanLengthRcp = 1.0/spanLength;
    FxaaBool directionN = dstN < dstP;
    FxaaFloat dst = min(dstN, dstP);
    FxaaBool goodSpan = directionN ? goodSpanN : goodSpanP;
    FxaaFloat subpixG = subpixF * subpixF;
    FxaaFloat pixelOffset = (dst * (-spanLengthRcp)) + 0.5;
    FxaaFloat subpixH = subpixG * fxaaQualitySubpix;
    FxaaFloat pixelOffsetGood = goodSpan ? pixelOffset : 0.0;
    FxaaFloat pixelOffsetSubpix = max(pixelOffsetGood, subpixH);
    if(!horzSpan) posM.x += pixelOffsetSubpix * lengthSign;
    if( horzSpan) posM.y += pixelOffsetSubpix * lengthSign;
    return FxaaFloat4(FxaaTexTop(tex, posM).xyz, lumaM);
}

  float3 saturation(float3 rgb, float adjustment)
  {
    const float3 W = float3(0.2125, 0.7154, 0.0721);
    float intensity = dot(rgb, W);
    return lerp(float3(intensity, intensity, intensity), rgb, adjustment);
  }

 struct PS_INPUT
  {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 edgeSampleUV0 : TEXCOORD1;
    float2 edgeSampleUV1 : TEXCOORD2;
    float2 edgeSampleUV2 : TEXCOORD3;
    float2 edgeSampleUV3 : TEXCOORD4;
    float2 sampleStepSize : TEXCOORD5;
    float saturation : TEXCOORD6;
  };

  struct PS_OUTPUT
  {
    float4 Color0 : SV_Target;
  };

  sampler sampler0;
  Texture2D texture0;

  sampler sampler1;
  Texture2D texture1;

  PS_OUTPUT main(PS_INPUT input)
  {
    PS_OUTPUT output;
    float4 colour = float4(0.0, 0.0, 0.0, 0.0);
    float depth = texture1.Sample(sampler1, input.uv).x;

    // only run FXAA on edges (simple edge detection)
    float depth0 = texture1.Sample(sampler1, input.edgeSampleUV0).x;
    float depth1 = texture1.Sample(sampler1, input.edgeSampleUV1).x;
    float depth2 = texture1.Sample(sampler1, input.edgeSampleUV2).x;
    float depth3 = texture1.Sample(sampler1, input.edgeSampleUV3).x;

    const float edgeThreshold = 0.003;
    float isEdge = 1.0 - (step(abs(depth0 - depth), edgeThreshold) * step(abs(depth1 - depth), edgeThreshold) * step(abs(depth2 - depth), edgeThreshold) * step(abs(depth3 - depth), edgeThreshold));
    if (isEdge == 0.0)
    {
      colour = texture0.Sample(sampler0, input.uv);
    }
    else
    {
      FxaaTex samplerInfo;
      samplerInfo.smpl = sampler0;
      samplerInfo.tex = texture0;
      
      colour = FxaaPixelShader(input.uv, float4(0, 0, 0, 0), samplerInfo, samplerInfo, samplerInfo, input.sampleStepSize,
                                     float4(0, 0, 0, 0), float4(0, 0, 0, 0), float4(0, 0, 0, 0),
                                     0.75,  //fxaaQualitySubpix
                                     0.125, // fxaaQualityEdgeThreshold
                                     0.0, // fxaaQualityEdgeThresholdMin
                                     0, 0, float4(0, 0, 0, 0));
    }
 
    output.Color0 = float4(saturation(colour.xyz, input.saturation), 1.0);
    return output;
  }
)shader";
