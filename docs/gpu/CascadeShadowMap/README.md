# Cascade Shadow Map

<img width="1915" height="1074" alt="Image" src="https://github.com/user-attachments/assets/fb9c6ad7-cdd4-4a46-82c4-9c477079d98f" />

<img width="1917" height="1076" alt="Image" src="https://github.com/user-attachments/assets/be2c222b-b2a3-446c-acac-c903827689b2" />

<img width="1915" height="1075" alt="Image" src="https://github.com/user-attachments/assets/05bb0e7a-a34c-42ce-bd7e-c00b3275f5f0" />

## 1. 개요

Voxen의 그림자는 단일 Directional Light(태양)에 대한 **3-Cascade Shadow Map(CSM)** 으로 구성된다.

0. CSM의 Buffer는 `WIDTH*HEIGHT = 1024*1024`로 구성하고 3개의 slice로 Texture2DArray를 사용한다.

1. Light Projection Matrix를 구성 방식에는 `Camera-Center` / `Frustum-Splits` 으로 구분한다.

- 프로젝트는 `Camera-Center`를 사용하며 `Frustum-Splits`은 비교용으로 사용중이다.

2. shimmering 방지를 위한 Texel Snap을 구성한다.

3. ShadowMap의 적절한 Cascade를 선택하기 위해 `Interval-Based` / `Map-Based` 로 구분한다.

- 프로젝트는 `Map-Based`를 사용하며 `Interval-Based`를 비교용으로 사용중이다.

4. Cascade 전환 시 끊김을 제거하기 위해 Cascade Blending을 사용한다.

## 2. 도입 동기

좁은 반경의 월드가 아닌, 넓은 반경의 월드를 렌더링해야했기에 단일 Shadow Map으로는 해상도가 부족했다.

일반적으로 그림자 구현을 위해서 CSM을 사용하기도 하기에 적용시켰다.

MS 문서에는 Cascade Shadow Map에 대한 설명이 존재하고 directx-sdk-samples로 어느정도 참고하여 작성할 수 있을 것 같아 CSM을 도입했다.

MS Docs: https://learn.microsoft.com/ko-kr/windows/win32/dxtecharts/cascaded-shadow-maps
MS Sample Code: https://github.com/walbourn/directx-sdk-samples-reworked/blob/main/CascadedShadowMaps11/CascadedShadowsManager.cpp

## 3. 핵심 아이디어

### 3.1 Texture2DArray

`WIDTH(1024*3) * HEIGHT(1024)`를 하나의 버퍼로 구성하기 보단, `WIDTH(1024) * HEIGHT(1024) * SLICE(3)` 를 두어 구성한다.

이는 Cascade간의 영역침범을 막기위해 사용되었다.

샘플링 시에 다른 Cascade를 넘어갈 일이 없어 보다 안정적이게 사용할 수 있었다.

### 3.2 Geometry Shader

Light View에 들어오는 모든 Chunk들을 RTV-ArraySlice마다 렌더링하는 것을 비효율적이기에 GS를 구성하여 하나의 삼각형을 3개의 삼각형으로 출력한다.

GS의 출력으로 사용되는 System-Value는 `SV_RenderTargetArrayIndex`로, 출력되는 Array Slice를 지정하게 된다.

참고로, `SV_ViewportArrayIndex`로 출력되는 Viewport Slice Index도 지정할 수 있다.

### 3.3 Lighting Projection Matrix 구성

결국 GS에서 사용되는 3가지의 Projection Matrix가 필요하다.

Directional Lighting에 사용되므로 정투영을 사용한다.

`XMMatrixOrthographicOffCenterLH` 함수를 사용하게 되는데 `min / max.xy`, `nearZ / farZ` 값을 지정해줘야 한다.

이 Light에 투영될 박스를 구성하는 방식은 두가지로 설정했다. -> 모두 Fit-To-Scene 방식

1. 카메라 중심으로한 정육면체 박스 구성(Camera-Center)

2. 카메라 Frustum을 적절히 잘라 박스 구성 (Frustum Splits)

### 3.4 View에 대한 Shimmering 제거

MS문서에 Texel에 WorldSize를 Snap하는 코드가 존재했고, 이를 적용시켰다.

```
minV /= WorldSizePerTexel;
minV = floor(minV)
minV *= WorldSizePerTexel;
```

### 3.5 Cascade 선택 방식

CSM이 구성되고 Shading 시에 샘플링하게 되는데 어떤 Cascade에서 샘플링하는지에 대한 방법이다.

MS 문서를 보니, Interval-Based / Map-Based가 존재했다.

Interval 기반은 CameraView.Z 거리를 이용하여 Cascade를 결정하고, Map기반은 NDC 좌표를 이용하여 Cascade를 결정한다.

### 3.6 Cascade Blending

Map-Based는 NDC 좌표를 이용하여 가중치를, Interval-Based는 View.z를 이용하여 Blend 가중치를 설정하여 Cascade를 섞었다.

마지막 Level의 Cascade인 경우 단순히 서서히 그림자가 사라지게끔 구성했다.

## 4. 구현 내용

### 4.1 ShadowGS.hlsl

Light Shadow의 `viewProj[3]`을 받아 사용한다.

VS에서 넘어온 삼각형 한 개를 3개로 복제하는데, Texture2DArray를 사용하므로 `SV_RenderTargetArrayIndex`를 사용하여 출력한다.

실행되는 PS는 없지만, DSV에 적절히 Array Index를 구분하여 Depth 값이 들어가게 된다.

```
cbuffer ShadowConstantBuffer : register(b0)
{
    Matrix viewProj[CASCADE_LEVEL];
}
...
struct gsOutput
{
    float4 posProj : SV_POSITION;
    uint RTIndex : SV_RenderTargetArrayIndex;
};

[maxvertexcount(9)]
void main(triangle gsInput input[3], inout TriangleStream<gsOutput> output)
{
    gsOutput element;

    for (int cascade = 0; cascade < CASCADE_LEVEL; ++cascade)
    {
        element.RTIndex = cascade;

        for (int i = 0; i < 3; ++i)
        {
            float4 position = float4(input[i].posWorld.xyz, 1.0);
            element.posProj = mul(position, viewProj[cascade]);

            output.Append(element);
        }
        output.RestartStrip();
    }
}
```

### 4.2 Cascade Lighting View Projection Matrix (Light.cpp - Update())

GS에서나 Shadow Factor를 구할 때 결국 Lighting View Projection을 해야하는데 Matrix을 잘 만드는게 어려운 과정이다.

Lighting View Box를 구성하는 방식 2가지를 구현하고 Shimmering 제거를 위한 WorldSize Texel Snap을 구현했다.

#### Lighting View Box - 1. Fit-to-Scene of Camera-Center (현재 사용)

카메라 위치를 라이트 공간으로 변환하고, 그 주위로 한 변이 `2 × halfSize` 인 정육면체를 만든다.

이는 Cascade가 카메라 중심으로 박스를 구성하고, Cascade가 커짐에 따라 작은 Cascade Box를 포함하게 된다.

Frustum에 보이지 않는 부분도 렌더링하는 해상도 낭비가 존재하지만, 구현이 간단하고 직관적이다.

```cpp
// Light.cpp::FitToSceneOfCenter
const float cascadeHalfSizes[CASCADE_LEVEL] = { 24.0f, 60.0f, 150.0f };

Vector3 lightViewCameraPos = Vector3::Transform(camera.GetPosition(), lightShadowViewMatrix);

for (int cascadeIndex = 0; cascadeIndex < CASCADE_LEVEL; ++cascadeIndex)
{
    float halfSize = cascadeHalfSizes[cascadeIndex];
    float boxSize  = halfSize * 2.0f;

    Vector3 lightViewPointsMin = lightViewCameraPos - Vector3(halfSize);
    Vector3 lightViewPointsMax = lightViewCameraPos + Vector3(halfSize);

    // Texel Snap
    float worldUnitsPerTexel = boxSize / (float)CASCADE_SIZE;
    float minX = std::floor(lightViewPointsMin.x / worldUnitsPerTexel) * worldUnitsPerTexel;
    ...
    lightViewPointsMin = Vector3(minX, minY, minZ);
    lightViewPointsMax = lightViewPointsMin + Vector3(boxSize);

    m_proj[cascadeIndex] = XMMatrixOrthographicOffCenterLH(...);
}
```

#### Lighting View Box - 2. Fit-to-Scene of Frustum-Splits (보조 구현, 토글로 비교용)

`FitToSceneOfSplits`는 카메라 View Frustum을 cascade 비율로 잘라 그 sub-frustum의 라이트 공간 AABB를 박스로 쓰는 MS 샘플 방식이다.

- 동적 AABB는 박스 크기가 매 프레임 달라져 — Texel Snap이 무력화된다.
- 이를 보정하기 위해 `constantDiagonals` 로 박스를 “가장 큰 대각선 ≥ 모든 회전” 크기로 패딩한다.
  - Frustum Splits을 고정된 크기 Box로 설정하는 이유는 아래에서 다룬다. (5.2 문제점 & 해결)

```cpp
const float cascadeScale[CASCADE_LEVEL + 1] = { 0.00f, 0.01f, 0.03f, 0.15f };
const float constantDiagonals[CASCADE_LEVEL] = { 36.0f, 110.0f, 450.0f };
...
Vector3 vDiagonal(constantDiagonals[cascadeIndex]);
Vector3 maxminVector = lightViewPointsMax - lightViewPointsMin;
Vector3 borderOffset = (vDiagonal - maxminVector) * 0.5f;
lightViewPointsMax += borderOffset;
lightViewPointsMin -= borderOffset;
```

#### Stable Shadow Snap - 카메라 움직임에 따른 Shimmering 제거

박스 크기가 고정되어 있다는 전제 하에, 박스의 `min` 좌표를 **한 텍셀 크기 단위로 양자화** 한다.

- 박스 크기가 고정이 아닌 경우 `worldUnitsPerTexel`가 변동되고, 변화되는 값으로 양자화하는 건 의미가 없다.
- 박스 크기가 고정된 경우 `worldUnitsPerTexel` 값도 고정된다.
- 이는 카메라가 한 텍셀보다 작게 움직이는 동안에는 박스 자체가 움직이지 않고 Texel에 맺히는 World Size가 고정되어 Shimmering 제거된다.
- World Size가 한 텍셀에 고정되는게 Shimmering을 제거하는 이유
  - 같은 WorldSize를 임의의 텍셀 한칸에서 샘플링을 할 수 있기 때문이다.
  - 이는 정투영(orthographic)의 방식을 이용하여 Shadow Map을 작성하여 가능한 것이다.

```cpp
float worldUnitsPerTexel = boxSize / (float)CASCADE_SIZE;

float minX = std::floor(lightViewPointsMin.x / worldUnitsPerTexel) * worldUnitsPerTexel;
float minY = std::floor(lightViewPointsMin.y / worldUnitsPerTexel) * worldUnitsPerTexel;
float minZ = std::floor(lightViewPointsMin.z / worldUnitsPerTexel) * worldUnitsPerTexel;

lightViewPointsMin = Vector3(minX, minY, minZ);
lightViewPointsMax = lightViewPointsMin + Vector3(boxSize); // box 크기 고정이므로 max도 자동 정렬
```

### 4.3 Cascade Selection (Shadow.hlsli)

두 가지 방식을 이용하여 Cascade를 선택한다.

1. Map-Based: 투영된 NDC 좌표를 이용하여 가장 정밀도가 높은 Cascade를 선택
2. Interval-Based: CameraView.z 값을 이용하여 프러스텀 범위에 들어온 Cascade를 선택

#### Map-Based (현재 사용)

각 cascade에 대해 라이트 공간 NDC를 계산하고, **[-1, 1] 박스 안에 들어오는 첫 cascade** 를 선택한다.

불필요한 해상도 낭비가 없다고 판단되어 현재 프로젝트의 기본으로 설정해놓았다.

```hlsl
void mapBasedCascadeSelection(float3 posWorld, float NdotL, inout uint outCascade, out float4 outLightProj)
{
    [unroll]
    for (uint i = 0; i < cascadeLevel; ++i)
    {
        float bias = getCascadeBias(i, NdotL);

        float4 lightProj = mul(float4(posWorld, 1.0), shadowViewProj[i]);
        lightProj.xyz /= lightProj.w;

        if (InBound(lightProj.xyz, 1e-3, 1e-3, bias))
        {
            outCascade = i;
            outLightProj = lightProj;
            break;
        }
    }
}
```

#### Interval-Based (비교 토글용)

Interval-Based는 추가적인 데이터가 필요하다.

Cascade를 선택할 기준점이 필요한데, CemeraFrustum이 어느 부분에서 잘렸는지에 대한 기준점이 필요하다.

예를 들면, `farZ`가 `900.0f` 이고 3등분했다면 `[0.0f 300.0f 600.0f, 900.0f]`처럼 z 거리 값이 필요해진다. -> `cascadeDistance[4]`

참고로 Frustum-Split으로 LightBox를 구성할 땐 Z 값을 그대로 사용할 수 있어 문제가 없지만
Camera-Center로 LightBox를 구성한 값을 그대로 사용하면 xy 범위를 벗어날 수 있다.

```hlsl
void intervalBasedCascadeSelection(float3 posWorld, float NdotL, inout uint outCascade, out float4 outLightProj)
{
    float viewDistZ = dot(posWorld - eyePos, eyeDir); // viewZ: |posWorld-eyePos| * |eyeDir| * cosTheta
    float cascadeDistance[4] = { cascadeSplits.x, cascadeSplits.y, cascadeSplits.z, cascadeSplits.w };

    [unroll]
    for (uint i = 0; i < cascadeLevel; ++i)
    {
        if (cascadeDistance[i] <= viewDistZ && viewDistZ < cascadeDistance[i + 1])
        {
            outCascade = i;

            float4 lightProj = mul(float4(posWorld, 1.0), shadowViewProj[i]);
            lightProj.xyz /= lightProj.w;

            outLightProj = lightProj;
            break;
        }
    }
}
```

### 4.4 부드러운 Cascade 경계처리 - Cascade Blending

cascade 간 경계가 끊겨 보이는 문제를 완화하기 위해, **현재 cascade의 박스 경계 근처에서 다음 cascade를 함께 샘플링** 하여 보간한다.

#### Map-Based Blending

NDC에서 박스 안쪽으로 얼마나 들어와 있는지를 측정한다.

- `NdcDist`: NDC 중앙의 값이 1, 경계 부분에서 0으로 수렴한다.

이 때 단순히 투영되는 NDC.xy 만을 연산하는게 아닌 z 값도 거리범위에 포함한다.

- CSM만을 생각하면 샘플링되는 xy만 조사하면 될 것 같지만, 실제로는 Light Box.Z 범위에 들어오지 못하여 다음 Cascade에 상이 맺히는 경우가 존재하기 때문이다.

`blendRange`보다 `NdcDist`가 크면(안쪽인 경우) Blending을 하지 않는다.

```hlsl
float NdcDistXY = min(min(proj.x + 1.0, 1.0 - proj.x),
                      min(proj.y + 1.0, 1.0 - proj.y)); // 박스 중앙→1, 경계→0
float NdcDistZ  = min(proj.z, 1.0 - proj.z) * 2.0;       // z[0,1] → 중앙(0.5)→1
float NdcDist   = min(NdcDistXY, NdcDistZ);

float blendWeight = 1.0 - smoothstep(0.0, blendRange, NdcDist);

if (blendWeight > 0.0)
    // blending
```

#### Interval-Based Blending

cameraZ에 대한 값을 가지고 BlendWeight를 결정한다.

```hlsl
float nearZ = cascadeDistance[cascadeIndex];
float farZ = cascadeDistance[cascadeIndex + 1];
float blendStartZ = lerp(farZ, nearZ, blendRange); // lerp 역순: farZ에 가까울 수록 Blend할 것

// viewDistZ가 blendStartZ 보다 작거나 같은 경우 blending을 하지 않게 됨
float viewDistZ = dot(posWorld - eyePos, eyeDir);
float blendWeight = smoothstep(blendStartZ, farZ, viewDistZ);

if (blendWeight > 0.0)
    // blending
```

#### 마지막 Cascade에서의 Blending

마지막 cascade에서 더 큰 cascade가 없으면, **blendWeight 만큼 “lit”에 가깝게 보간** 하여 그림자가 자연스럽게 페이드아웃 되도록 했다.

```hlsl
if (nextCascadeIndex == cascadeLevel)
{
    inOutPercentLit = lerp(inOutPercentLit, 1.0, blendWeight);
}
```

## 5. 문제점 & 해결

### 5.1 카메라가 살짝 움직일 때마다 그림자 가장자리가 깜빡임 (Shimmering)

**문제**: cascade 박스가 카메라를 따라 “부드럽게” 움직이면, 한 텍셀 안에서 박스 원점이 미세하게 이동 → 같은 월드 위치가 다른 텍셀에 매핑된다 → 매 프레임 그림자 윤곽이 한 픽셀씩 점프하며 깜빡임이 보임.

**해결**: `worldUnitsPerTexel` 단위 Snap.
https://github.com/walbourn/directx-sdk-samples-reworked/blob/main/CascadedShadowMaps11/CascadedShadowsManager.cpp

해당 해결책은 Lighting View가 정적으로 크기가 같았어야 했다.

Box-Size가 변동되는 경우 Texel당 WorldSize도 변화되어 Shimmering 문제는 동일했다.

### 5.2 Box 크기 고정에 따른 정밀도 낭비

**문제1**

- Box 크기를 고정하기 위해 Sub-Frustum의 최대 대각선 길이를 사전에 찾고 Padding을 주고 Box 크기를 고정하니 해상도 낭비가 심했다.
- FrustumSplits Box 구성 형태에서 Interval Based Cascade를 선택하면 큰 해상도로 렌더링한 CSM이 낭비가 심했다.

**해결**

- Map-Based 기반 Cascade 선택 방식
- 가장 정밀도가 높은 Cascade를 우선으로 찾으니 전보다 해상도 낭비는 줄었다.

### 5.3 Cascade 샘플링 시 경계 문제

**문제1** : Cascade 경계면에서 다른 Cascade를 PCF 하는 문제가 발생했다.

**1차 해결**: Texture2D 형태에서 Texture2DArray로 구성했다. Cascade 경계를 넘어 pcf 하는 문제는 사라졌다.

**문제2**: Texture2DArray를 구성해도 Sampler State Border 값에 따라 그림자가 생겨야할 때 안생기고, 안생겨야할 때 생기는 문제가 발생했다.
`Border[0]` 값이 크면 경계에서 Lit이 발생하여 그림자가 생겨야할 곳에 안생기고, `Border[0]` 값이 작으면 그림자가 안생겨야할 곳에 생겼다.

**2차 해결**: Border 자체 비교를 하지 않게끔, Margin을 두고 샘플링했다.

```
abs(ndc.x) < 1.0 - xMargin && abs(ndc.y) < 1.0 - yMargin;
```

**문제3**: NDC.z 값에 대한 경계문제

<img width="800" height="500" alt="Image" src="https://github.com/user-attachments/assets/919767d0-0554-4641-9245-b716108eca53" />

```
shadowTex.SampleCmpLevelZero(ss, uvw, proj.z - bias).r;
```

`proj.z - bias`를 이용하여 Compare하게 되는데, 이 값 자체가 특정한 상황에서 음수가 되어 적절하지 않은 비교를 하게 되었다. 또한 0으로 clamp를 해도 적절하지 않다.

- 예시1: ShadowSS `CompareFunc = LESS`
  - 같은 깊이일 때 그림자가 생기지 않아야 하지만 그림자가 생김
  - ShadowMap에도 Depth가 `0`이고(shadowMapRS: `DepthClipEnable=false`), `proj.z`의 값이 `0`일 때
  - `proj.z - bias`가 0으로 clamped 되어 `0 < 0` 을 비교 -> Lit이 발생하지 않고 그림자가 생김
  - 그렇다고 clamp 하지 않으면 bias가 proj.z보다 커서 음수가 되고, `0 < -` 비교로 그림자가 생겨야할 부분에 그림자가 안생김
- 예시2: ShadowSS `CompareFunc = LESS_EQUAL`
  - `0 <= 0` 비교하게 되어 Lit이 발생
  - 실제로 더 깊은 곳에 있어서 그림자가 생겨야 할 부분에 그림자가 생기지 않음

**해결**: z값에 대한 margin은 bias로 설정함

- 추가로 근본 문제는 bias가 커서 그렇다. 적당한 크기로 수정했다.

```
bool InBound(float3 ndc, float xMargin, float yMargin, float zMargin)

if ( InBound(lightProj.xyz, xMargin, yMargin, bias) )
```

### 5.4 Map-Based Blending에서의 z 값

**문제**

<img width="800" height="500" alt="Image" src="https://github.com/user-attachments/assets/6941e8f9-7b03-48cb-a87e-359fa5210dcd" />

- 노란색이 Blending 되는 곳
- 초기 `NdcDist`는 xy만 검사했다. 그 이유는 단순히 텍스쳐를 샘플링할 때, 먼 곳에서만 블랜딩을 하면 되는 줄 알았다.
- CSM 텍스쳐의 x에 대해서는 Blending이 잘됐지만, 위아래(Light가 바라보는-y) 방향에서의 Blending이 없었다.
- NDC.y값이 문제인 줄 알았으나, 실제로 샘플링 하는 곳은 y의 중앙부분이였고, Blending 되어야 하는 부분은 z값이었다.

**해결**: z도 같이 측정.

<img width="800" height="500" alt="Image" src="https://github.com/user-attachments/assets/86b12100-043b-429d-ba53-574f707c1a85" />

```hlsl
float NdcDistZ = min(proj.z, 1.0 - proj.z) * 2.0; // z는 [0,1]이라 *2로 xy와 정규화
float NdcDist  = min(NdcDistXY, NdcDistZ);
```

- xy는 [-1, 1]이라 `min(p+1, 1-p)`가 0~1
- z는 [0, 1]이라 `min(z, 1-z)`가 0~0.5 → 같은 스케일로 비교하려면 ×2
- 박스의 어느 면이든 가까워지면 자연스럽게 블렌딩

### 5.5 CSM에서 여러가지 상황에 대한 트레이드 오프 고찰

**문제**: 고려해야 하는 부분이 상당히 많은 Cascade Shadow Map였고, 하나를 취하면 다른 하나가 다른 문제로 발생했다.

**트레이드오프**

- 고정된 크기의 Box vs 동적 box
  - 고정된 크기의 Box -> Texel Snap으로 Shimmering 제거 -> 해상도 낭비
  - 동적 크기의 Box -> 해상도 낭비가 줄어듦 -> Shimmering 제거 방법에 대한 의문
  - 선택: Shimmering이 품질을 더 해치므로 고정된 크기의 Box 사용

- Cascade 선택 방식 (Interval vs Map Based)
  - Interval Based: 표준 -> 내 코드에서 해상도 낭비가 심함
  - Map-Based: 더 좋은 해상도를 샘플링 -> 카메라 거리에 대한 연산이 없으므로 표준적 못한 느낌이었음
  - 선택: 더 좋은 해상도를 가진 CSM를 샘플링하는게 렌더링 결과물이 더 좋았기에 Map-Based 사용하고 토글 형태로 Interval 사용

- 적절한 Bias 고르기
  - Bias 작음: Acne
  - Bias 큼: Peter Panning
  - 선택: Acne이 렌더링 품질을 해치기에 bias를 좀 크게 설정함
  - cf. 완벽한 Bias를 지정하는 방법을 모름

- Fit-to-Scene Box 구성 방식 (CameraCenter vs Frustum Split)
  - Frustum Split: MS에서 사용하는 방식과 유사, 구현이 복잡하고 박스의 크기가 직관적이지 않음
  - Camera Center: 완전 직관적이고 구현이 간단함, 보지 않는 곳도 결국 렌더링
  - 선택: 결국 둘 다, 해상도를 낭비하게 되므로 기본설정으로 Cemera Center의 고정박스를 사용하고 토글 형태로 Frustum Split 사용

## 6. 회고 및 미흡한 점

- 그림자의 원리는 되게 단순하다.
  - Light로 렌더링 -> PS에서 (VP)-1 역산하여 Light에 정투영 -> 샘플링 -> 깊이 검사

- Shadow는 RS 환경에서 어려운 과제인 것 같다.
  - 지금까지 voxen중에 가장 어렵고 수정하기 난해한 챕터다.
  - 하나를 고치면 다른 하나의 문제가 발생하고, 트레이드오프 상황도 많았다.
  - Shimmering 제거하는 방식이 처음에 잘 와닿지 않아 코드를 이해하는데 한참이 걸렸다.
  - Projection Matrix를 구하는 법 Cascade를 선택하는 방식에 따라 고려할 게 정말 많았다.
  - Cascade 경계 문제에서 어떤 값이 문제인지 아니면 Sampler State가 잘못되었는지 판단하기 너무 어려웠다.

- 결과는 만족하나 미흡한점이 많이 있다.
  - Bias: 현재는 hard 설정되어 있어서 Light View Box에 따라 bias가 달라지고, Peter Panning이 존재한다.
  - Light 움직임에 따른 Shimmering: View에 대해서는 stable하지만 Light가 움직이면 깜빡거린다. 각도 회전에 대해서 양자화했지만 크게 달라지지 않았다.
  - 해상도 낭비: 어떤 방식을 선택하던지 Shimmering을 제거함에 있어서 해상도 낭비가 심하다.

- Piter Panning은 Front Face Culling을 하고 BackFace를 살리면 일반적인 프로젝트에서는 해결이 가능한 것으로 보인다.

<details>
<summary>Shadow.hlsli (클릭해서 펼치기)</summary>

```hlsl
#ifndef SHADOW_HLSLI
    #define SHADOW_HLSLI

#include "Common.hlsli"

#define BLEND_CASCADE (cascadeLevel + 1)
#define BLEND_LAST_CASCADE (cascadeLevel + 2)

Texture2DArray shadowTex : register(t13);

float3 getCascadeColor(uint cascade)
{
    float3 cascadeColor = float3(0.0, 0.0, 0.0);

    if (cascade == 0)
        cascadeColor = float3(1, 0, 0);
    else if (cascade == 1)
        cascadeColor = float3(0, 1, 0);
    else if (cascade == 2)
        cascadeColor = float3(0, 0, 1);
    else if (cascade == BLEND_CASCADE)
        cascadeColor = float3(1, 1, 0);
    else if (cascade == BLEND_LAST_CASCADE)
        cascadeColor = float3(0.5, 0.5, 0.5);
    else
        cascadeColor = float3(0.25, 0.25, 0.25);

    return cascadeColor;
}

float getCascadeBias(uint cascadeIndex, float NdotL)
{
    const float baseBias[3] = { 0.0025, 0.002, 0.002 };
    const float slopeBias[3] = { 0.005, 0.005, 0.002 };

    return baseBias[cascadeIndex] + slopeBias[cascadeIndex] * pow(1.0 - NdotL, 3.0);
}

float sampleCascade(float4 proj, uint cascadeIndex, float bias)
{
    float2 lightTexcoord = float2(proj.x * 0.5 + 0.5, -proj.y * 0.5 + 0.5);

    return shadowTex.SampleCmpLevelZero(
        shadowCompareSS, float3(lightTexcoord, cascadeIndex), proj.z - bias).r;
}

bool InBound(float3 ndc, float xMargin, float yMargin, float zMargin)
{
    zMargin = 0;
    return abs(ndc.x) < 1.0 - xMargin && abs(ndc.y) < 1.0 - yMargin && zMargin < ndc.z && ndc.z < 1.0;
}

void mapBasedCascadeSelection(float3 posWorld, float NdotL, inout uint outCascade, out float4 outLightProj)
{
    [unroll]
    for (uint i = 0; i < cascadeLevel; ++i)
    {
        float bias = getCascadeBias(i, NdotL);

        float4 lightProj = mul(float4(posWorld, 1.0), shadowViewProj[i]);
        lightProj.xyz /= lightProj.w;

        if (InBound(lightProj.xyz, 1e-3, 1e-3, bias))
        {
            outCascade = i;
            outLightProj = lightProj;
            break;
        }
    }
}

void intervalBasedCascadeSelection(float3 posWorld, float NdotL, inout uint outCascade, out float4 outLightProj)
{
    float viewDistZ = dot(posWorld - eyePos, eyeDir); // viewZ: |posWorld-eyePos| * |eyeDir| * cosTheta
    float cascadeDistance[4] = { cascadeSplits.x, cascadeSplits.y, cascadeSplits.z, cascadeSplits.w };

    [unroll]
    for (uint i = 0; i < cascadeLevel; ++i)
    {
        if (cascadeDistance[i] <= viewDistZ && viewDistZ < cascadeDistance[i + 1])
        {
            outCascade = i;

            float4 lightProj = mul(float4(posWorld, 1.0), shadowViewProj[i]);
            lightProj.xyz /= lightProj.w;

            outLightProj = lightProj;
            break;
        }
    }
}

bool blendMapBased(float blendRange, uint cascadeIndex, float3 posWorld, float4 proj, float NdotL,
                        inout float inOutPercentLit, inout uint outCascadeIndex)
{
    float NdcDistXY = min(min(proj.x + 1.0, 1.0 - proj.x),
                               min(proj.y + 1.0, 1.0 - proj.y)); // NDC.xy 중앙(1,1)->1, 측면->0
    float NdcDistZ = min(proj.z, 1.0 - proj.z) * 2.0; // NDC.z 중앙(0.5)->1
    float NdcDist = min(NdcDistXY, NdcDistZ);

    float blendWeight = 1.0 - smoothstep(0.0, blendRange, NdcDist);

    if (blendWeight > 0.0)
    {
        uint nextCascadeIndex = cascadeIndex + 1;

        if (nextCascadeIndex == cascadeLevel)
        {
            inOutPercentLit = lerp(inOutPercentLit, 1.0, blendWeight);
            outCascadeIndex = BLEND_LAST_CASCADE;
            return true;
        }

        float nextBias = getCascadeBias(nextCascadeIndex, NdotL);

        float4 nextLightProj = mul(float4(posWorld, 1.0), shadowViewProj[nextCascadeIndex]);
        nextLightProj.xyz /= nextLightProj.w;

        if (InBound(nextLightProj.xyz, 1e-3, 1e-3, nextBias))
        {
            float nextPercentLit = sampleCascade(nextLightProj, nextCascadeIndex, nextBias);

            inOutPercentLit = lerp(inOutPercentLit, nextPercentLit, blendWeight);

            outCascadeIndex = BLEND_CASCADE;

            return true;
        }
    }

    return false;
}

bool blendIntervalBased(float blendRange, uint cascadeIndex, float3 posWorld, float NdotL,
                            inout float inOutPercentLit, inout uint outCascadeIndex)
{
    float viewDistZ = dot(posWorld - eyePos, eyeDir); // viewZ: |posWorld-eyePos| * |eyeDir| * cosTheta
    float cascadeDistance[4] = { cascadeSplits.x, cascadeSplits.y, cascadeSplits.z, cascadeSplits.w };

    float nearZ = cascadeDistance[cascadeIndex];
    float farZ = cascadeDistance[cascadeIndex + 1];
    float blendStartZ = lerp(farZ, nearZ, blendRange);

    // viewDistZ가 blendStartZ 보다 작거나 같은 경우 blending을 하지 않게 됨
    float blendWeight = smoothstep(blendStartZ, farZ, viewDistZ);

    if (blendWeight > 0.0)
    {
        uint nextCascadeIndex = cascadeIndex + 1;

        if (nextCascadeIndex == cascadeLevel)
        {
            inOutPercentLit = lerp(inOutPercentLit, 1.0, blendWeight);
            outCascadeIndex = BLEND_LAST_CASCADE;
            return true;
        }

        float nextBias = getCascadeBias(nextCascadeIndex, NdotL);

        float4 nextLightProj = mul(float4(posWorld, 1.0), shadowViewProj[nextCascadeIndex]);
        nextLightProj.xyz /= nextLightProj.w;

        float nextPercentLit = sampleCascade(nextLightProj, nextCascadeIndex, nextBias);
        inOutPercentLit = lerp(inOutPercentLit, nextPercentLit, blendWeight);

        outCascadeIndex = BLEND_CASCADE;

        return true;
    }

    return false;
}

float getShadowFactor(float3 posWorld, float3 normal, out uint outCascadeIndex)
{
    float NdotL = max(dot(lightDir, normal), 0.0);

    uint selectedCascade = cascadeLevel;
    float4 selectedCascadeLightProj;

    /*
    * 1. NDC를 기준으로 Cascade를 찾는 Map Based Cascade
    * 2. interval(depth, viewDistZ)를 가지고 Cascade를 찾는 Interval Based Cascade
    */
    if (useMapBasedCascade)
        mapBasedCascadeSelection(posWorld, NdotL, selectedCascade, selectedCascadeLightProj);
    else
        intervalBasedCascadeSelection(posWorld, NdotL, selectedCascade, selectedCascadeLightProj);

    outCascadeIndex = selectedCascade;

    // 적절한 cascade를 찾지 못한 경우 얼리리턴
    if (selectedCascade == cascadeLevel)
    {
        return 1.0;
    }


    float bias = getCascadeBias(selectedCascade, NdotL);
    float percentLit = sampleCascade(selectedCascadeLightProj, selectedCascade, bias);

    // blending cascade
    if (useCascadeBlend)
    {
        /*
        * blendMapBased: NDC 거리를 이용하여 BlendWeight 결정
        * blendIntervalBased: view Dist Z 값을 이용하여 BlendWeight 결정
        */
        if (useMapBasedCascade)
            blendMapBased(0.2, selectedCascade, posWorld, selectedCascadeLightProj, NdotL, percentLit, outCascadeIndex);
        else
            blendIntervalBased(0.2, selectedCascade, posWorld, NdotL, percentLit, outCascadeIndex);
    }

    float radianceShadowWeight = clamp(radianceWeight / maxRadianceWeight, 0.0, 1.0);
    return lerp(percentLit, 1.0, 1.0 - radianceShadowWeight);
}

#endif
```

</details>
