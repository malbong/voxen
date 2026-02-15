# Water — Mirror Reflection, Absorption Color, Underwater Filter

## 1. 개요

Voxen의 수면 렌더링 시스템은 **Planar Reflection(평면 반사)** 기반의 거울 월드 렌더링과, **Beer-Lambert 흡수** 기반의 수중 투영색, **Fresnel 블렌딩**으로 반사-투영을 합성하는 Forward 렌더링 파이프라인이다. 추가로 수중 상태에서는 렌더링 순서를 변경하고 전용 Color Filter를 적용한다.

핵심 구성:

- **Mirror World** — 수면(Y=64)을 기준으로 반전된 세계를 별도 렌더 타겟에 렌더링
- **Water Color** — 투영색(Beer-Lambert) + 반사색(Mirror) + 수면 자체색(Water Albedo)을 Fresnel로 합성
- **Underwater Filter** — 수중 시 렌더링 순서 변경 + 푸른 색 필터 + 안개 거리 단축

## 2. 도입 동기

수면은 게임 월드에서 가장 넓은 면적을 차지하는 반투명 오브젝트 중 하나다. 단순히 반투명한 파란 면을 그리면, **수면의 반사, 깊이에 따른 색 변화, 수중에서의 시야 변화**가 모두 누락되어 비현실적이다.

물리적으로 수면의 시각적 특성은:

- **반사(Reflection)**: 수면이 거울처럼 주변 환경을 비추며, 시야각이 낮을수록(Grazing Angle) 반사가 강해진다 (Fresnel 효과)
- **흡수(Absorption)**: 물을 통과하는 빛은 거리에 따라 지수적으로 감쇠한다 (Beer-Lambert 법칙)
- **산란(Scattering)**: 수중에서 시야가 흐려지고 푸르게 변한다

이 세 가지를 합산하면 자연스러운 수면을 표현할 수 있다.

## 3. 핵심 아이디어

### 3.1 Planar Reflection (Mirror World)

환경 반사를 표현하는 가장 직접적인 방법은 **수면을 기준으로 세계를 상하 반전**하여 한 번 더 렌더링하는 것이다. 이 "거울 세계"의 렌더링 결과가 수면의 반사 이미지가 된다.

```
    실제 세계          거울 세계 (Y=64 기준 반전)

    ☁ 구름
    🌲 나무              ▽ 나무 (반전)
━━━━━━━━━━━━━━━━  Y=64 (수면) ━━━━━━━━━━━━━━━━
    ~ 수면 ~           ☁ 구름 (반전)
```

반사 행렬(`Matrix::CreateReflection`)은 Y=64 평면을 기준으로 모든 정점의 Y좌표를 반전시킨다. 이 행렬을 View Matrix에 곱하면, 마치 수면 아래에서 위를 올려다보는 카메라로 렌더링하는 것과 같다.

### 3.2 Beer-Lambert 흡수와 Fresnel 합성

수면 아래의 오브젝트가 보이는 정도는 **물을 통과한 거리**에 따라 결정된다:

```
Beer-Lambert: absorption = 1 - exp(-coefficient × distance)
```

- 가까운 오브젝트(얕은 물): `distance` 작음 → 투명하게 보임
- 먼 오브젝트(깊은 물): `distance` 큼 → 수면 자체 색상으로 대체

반사(Mirror)와 투영(Projection)의 비율은 **Schlick Fresnel**로 결정한다:

```
Fresnel: 수직으로 보면 → 투과 우세 (물속이 보임)
         비스듬히 보면 → 반사 우세 (거울처럼 비침)
```

### 3.3 수중 렌더링 순서 변경

수면 위와 수면 아래에서 렌더링 순서가 달라야 한다:

- **수면 위**: Mirror World → Water Plane → Fog → Sky → Cloud
- **수면 아래**: Fog → Sky → Cloud → Water Plane

수중에서는 수면이 **위에** 있으므로, 하늘/구름을 먼저 그리고 그 위에 수면을 덮어야 한다. 또한 수중에서는 Mirror World를 렌더링하지 않는다 — 물 아래에서는 수면 반사가 보이지 않기 때문이다.

## 4. 구현 내용

### 4.1 Mirror World 렌더링 (App.cpp::RenderMirrorWorld)

Mirror World는 4단계로 구성된다:

#### (1) Stencil Masking — 수면이 보이는 영역만 표시

```cpp
// mirrorMaskingPSO: stencilMaskDSS + stencilRef=1
Graphics::SetPipelineStates(Graphics::mirrorMaskingPSO);
ChunkManager::GetInstance()->RenderTransparency();
```

수면(Transparency) 메시를 렌더링하되, 색상은 쓰지 않고 **Stencil Buffer에만 1을 기록**한다. 이후 단계에서 Stencil=1인 영역에만 Mirror World를 그려, 수면이 없는 곳에 반사가 그려지는 것을 방지한다.

`MirrorMaskingPS.hlsl`에서는 수면의 **깊이(depth)를 별도 렌더 타겟에 기록**한다:

```hlsl
float main(psInput input) : SV_Target0
{
    if (input.normal.y <= 0 || input.posWorld.y < 64.0 - 1e-4 || 64.0 + 1e-4 < input.posWorld.y)
        discard;

    float pixelDepth = input.posProj.z;

    // G-Buffer의 position에서 해당 위치의 기하 깊이를 로드
    float4 position = positionTex.Load(appScreenCoord, 0);
    float4 projPos = mul(mul(position, view), proj);

    if (projPos.z <= pixelDepth)  // 기하가 수면보다 앞에 있으면 → 수면이 가려짐
        discard;

    return pixelDepth;
}
```

- `input.normal.y <= 0` — 수면의 윗면만 처리 (아랫면 무시)
- `input.posWorld.y ≈ 64.0` — 수면 높이 확인
- **깊이 비교**: G-Buffer에 기록된 불투명 기하의 깊이가 수면보다 앞에 있으면 `discard` — 블록에 가려진 수면 픽셀에는 반사를 그리지 않는다
- 출력은 수면의 깊이값으로, 이후 Mirror World 렌더링에서 **Mirror Depth Texture**로 사용된다

#### (2) Mirror Sky + Mirror Cloud

```cpp
// Mirror View Matrix = ReflectionMatrix × ViewMatrix
m_constantData.view = m_mirrorPlaneMatrix * GetViewMatrix();

// Sky — Stencil 마스크 영역에만 그림
Graphics::SetPipelineStates(Graphics::skyboxMirrorPSO);  // mirrorDrawMaskedDSS, stencilRef=1
m_skybox.Render();

// Cloud — 반전된 카메라로 렌더링 + Winding 반전
Graphics::context->VSSetConstantBuffers(8, 1, m_camera.m_mirrorConstantBuffer.GetAddressOf());
Graphics::SetPipelineStates(Graphics::cloudMirrorPSO);  // mirrorRS (FrontCounterClockwise=true)
m_cloud.Render();
```

- **반사 행렬**: `Plane(Vector3(0, 64, 0), Vector3(0, 1, 0))`로 Y=64 평면 반사. View Matrix에 곱하여 뒤집힌 카메라를 구성
- **mirrorDrawMaskedDSS**: `StencilFunc = EQUAL`, `StencilRef = 1` — Stencil=1인 영역(수면 있는 곳)에만 렌더링
- **mirrorRS**: `FrontCounterClockwise = true` — 반전으로 삼각형 와인딩이 뒤집히므로, Rasterizer의 컬링 방향도 반전

#### (3) Mirror World (블록)

```cpp
ChunkManager::GetInstance()->RenderMirrorWorld();
```

반전된 카메라에서 블록을 렌더링한다. `basicMirrorPSO`를 사용하며, 이 PSO는:
- `mirrorRS` — 와인딩 반전
- `mirrorDrawMaskedDSS` — Stencil 마스크 영역에만 렌더링
- `basicMirrorPS` — Mirror Depth Texture를 참조하여, 수면보다 아래에 있는 블록만 그린다

BasicPS의 Mirror 변형(`mainMirror`)에서 `pixelDepth <= planeDepth`이면 discard하여, 수면보다 위에 있는 (반전 후에는 아래에 있는) 기하를 제거한다.

#### (4) Blur

```cpp
m_postEffect.Blur(3, Graphics::mirrorWorldSRV, Graphics::mirrorWorldRTV,
    Graphics::mirrorBlurSRV, Graphics::mirrorBlurRTV, Graphics::blurMirrorPS);
```

Mirror World를 **3회 Blur** 처리한다. 현실의 수면 반사는 완벽한 거울이 아니라 잔물결에 의해 흐릿하므로, Blur로 이를 근사한다.

Mirror World의 해상도는 원본의 절반(`APP_WIDTH/2 × APP_HEIGHT/2`)이다. 반사 이미지에는 높은 해상도가 필요 없고, 어차피 Blur로 흐릿해지므로 절반 해상도로 렌더링하여 성능을 절약한다.

### 4.2 Water Plane — 최종 수면 색상 합성 (WaterPlanePS.hlsl)

#### 수면 애니메이션

```hlsl
uint waterStillTextureArraySize = 32;
uint dateAmountPerSecond = dayCycleAmount / dayCycleRealTime;  // 24000 / 30 = 800
uint dateAmountPerIndex = dateAmountPerSecond / waterStillTextureArraySize;  // 800 / 32 = 25
uint waterStillTextureIndex = (dateTime % dateAmountPerSecond) / dateAmountPerIndex;
```

32장의 수면 텍스처(Still Water Atlas)를 게임 시간에 따라 순환하여 **물결 애니메이션**을 만든다. 1초(800 틱)에 32프레임이므로 약 1초 주기로 한 바퀴 순환한다.

#### Water Albedo — Climate 기반 수면 색상

```hlsl
float3 getWaterAlbedo(float2 texcoord, uint stillIndex, float3 worldPos, float3 normal)
{
    float alpha = waterStillAtlasTextureArray.Sample(pointWrapSS, float3(texcoord, stillIndex)).r;

    // climate noise → waterColorMap (잔디/잎과 동일한 패턴)
    float2 th = climateNoiseMapTex.SampleLevel(pointClampSS, climateTexcoord, 0.0).rg;
    float3 waterColor = waterColorMapTex.SampleLevel(pointClampSS, float2(th.x, 1.0 - th.y), 0.0).rgb;

    waterColor *= alpha;
    return waterColor;
}
```

수면 자체의 색상은 **Climate Noise(Temperature, Humidity)** 로 결정된다. GrassLeafColor 시스템과 동일한 방식으로, `waterColorMap` LUT에서 기후 값을 UV로 사용하여 바이옴별 수면 색상을 얻는다. Still 텍스처의 alpha 채널을 곱하여 물결 패턴에 따른 명암을 만든다.

#### Normal Mapping

```hlsl
float3 normalMapping(float2 texcoord, uint stillIndex)
{
    float3 normalTex = waterStillNormalAtlasTextureArray.Sample(...).rgb;
    normalTex = normalize(2.0 * normalTex - 1.0);

    // Water Plane은 수평이므로 TBN 고정:
    // T = (1, 0, 0), B = (0, 0, -1), N = (0, 1, 0)
    // (r, g, b) × TBN → (r, b, -g)
    return float3(normalTex.r, normalTex.b, -normalTex.g);
}
```

수면은 항상 Y=64의 수평면이므로, TBN 기저가 고정이다. 별도의 `getTangent()` 호출 없이 **행렬 곱을 성분 스위즐로 단순화**한다. Normal Map의 물결 패턴이 PBR 라이팅의 Specular에 반영되어 수면 하이라이트를 만든다.

#### Roughness 계산

```hlsl
float roughness = 0.2 / max(dot(mappedNormal, input.normal), 1e-3);
```

Normal Map으로 변형된 법선과 원래 법선(Y축)의 내적이 작을수록(물결이 심할수록) roughness가 높아진다. 잔잔한 부분은 매끈한 반사, 물결이 큰 부분은 거친 반사를 표현한다.

#### 수면 위: 투영 + 반사 + Fresnel 합성

```hlsl
// 1. Absorption — Beer-Lambert 법칙
float objectDistance = length(eyePos - originPosition);
float planeDistance = length(eyePos - input.posWorld);
float diffDistance = abs(objectDistance - planeDistance);
float absorptionCoeff = 0.075;
float absorptionFactor = 1.0 - exp(-absorptionCoeff * diffDistance);

float3 projColor = lerp(originColor, waterColor, absorptionFactor);

// 2. Mirror Reflection
float3 mirrorColor = mirrorWorldTex.Sample(linearClampSS, screenTexcoord).rgb;

// 3. Fresnel Blending
float3 toEye = normalize(eyePos - input.posWorld);
float3 reflectCoeff = float3(0.2, 0.2, 0.2);
float3 fresnelFactor = schlickFresnel(mappedNormal, toEye, reflectCoeff);

projColor *= (1.0 - fresnelFactor);
float3 blendColor = lerp(projColor, mirrorColor, fresnelFactor);
```

최종 색상은 3단계로 합성된다:

**1단계 — 투영색 (Projection Color)**

수면 아래에 있는 기하의 색상(`originColor`)을 시작점으로, **물을 통과한 거리**에 비례하여 수면 자체 색상(`waterColor`)으로 전환한다.

- `diffDistance` = 카메라-수면 거리와 카메라-수중 기하 거리의 차 = **물을 통과한 두께**
- `absorptionCoeff = 0.075` — 흡수 계수. 약 13블록(1/0.075) 깊이에서 63%(1-e⁻¹)가 흡수됨
- 얕은 물: `absorptionFactor ≈ 0` → `originColor`(수중 기하가 그대로 보임)
- 깊은 물: `absorptionFactor → 1` → `waterColor`(수면 자체 색으로 불투명)

**2단계 — 반사색 (Mirror Color)**

Mirror World 렌더링 결과(`mirrorWorldTex`)를 화면 좌표로 샘플링한다.

**3단계 — Fresnel 블렌딩**

Schlick Fresnel로 투영과 반사의 비율을 결정한다:

- `reflectCoeff = 0.2` (F0) — 수직으로 바라볼 때(0°) 반사율 20%
- 비스듬히 바라볼 때(~90°) → Fresnel → 1.0 — 거의 100% 반사
- `projColor × (1 - fresnel)` + `mirrorColor × fresnel` — 에너지 보존

#### 수중: 단순 혼합

```hlsl
if (isUnderWater)
{
    return float4(lerp(originColor, waterColor, 0.5), 1.0);
}
```

수중에서는 Fresnel이나 Mirror 없이, 기존 색상과 수면 색을 **50:50 혼합**한다. 수면을 아래에서 올려다보는 상황이므로 반사는 보이지 않고, 단순히 수면의 푸른빛이 투영된다.

### 4.3 Underwater Filter (WaterFilterPS.hlsl)

```hlsl
float4 main(psInput input) : SV_TARGET
{
    float3 renderColor = renderTex.Sample(linearClampSS, input.texcoord).rgb;
    float3 blendColor = lerp(renderColor, filterColor * clamp(radianceWeight, 0.1, 1.0), filterStrength);
    return float4(blendColor, 1.0);
}
```

수중 상태일 때 전체 화면에 **푸른 색 필터**를 적용한다.

#### CPU 파라미터 설정 (PostEffect.cpp)

```cpp
if (isUnderWater) {
    m_waterAdaptationTime += dt;
    float duration = m_waterAdaptationTime / m_waterMaxDuration;  // [0, 1] 적응도

    // Filter Color — 수중이 길어질수록 짙어짐
    filterColor.x = 0.075f + 0.075f * duration;   // R: 최대 0.15
    filterColor.y = 0.125f + 0.125f * duration;   // G: 최대 0.25
    filterColor.z = 0.48f  + 0.48f  * duration;   // B: 최대 0.96
    filterColor = SRGB2Linear(filterColor);

    // Filter Strength — 적응할수록 약해짐 (시야 확보)
    filterStrength = 0.9f - 0.5f * duration;       // 0.9 → 0.4

    // Fog — 수중에서 가시거리 단축
    fogDistMin = 15.0f + 15.0f * duration;          // 15 → 30
    fogDistMax = 30.0f + 90.0f * duration;          // 30 → 120
}
```

시간 경과에 따라 **"수중 적응"** 을 시뮬레이션한다:

| 파라미터 | 진입 직후 | 적응 완료 (2.5초) | 설명 |
|---|---|---|---|
| `filterColor` | 연한 청색 | 짙은 청색 | 수중 색감이 점점 강해짐 |
| `filterStrength` | 0.9 (강함) | 0.4 (약함) | 처음엔 앞이 안 보이다가 점점 시야 확보 |
| `fogDistMin/Max` | 15~30 | 30~120 | 안개 거리가 늘어나며 가시거리 회복 |

셰이더에서 `filterColor × clamp(radianceWeight, 0.1, 1.0)`로 밤에도 최소 밝기(0.1)를 보장하여, 야간 수중에서 완전히 검게 되는 것을 방지한다.

### 4.4 렌더링 순서 분기

```cpp
// Forward Render Pass
if (m_camera.IsUnderWater()) {
    RenderFogFilter();
    RenderSkybox();
    RenderCloud();
    RenderWaterPlane();     // 마지막: 수면이 위에 있으므로
}
else {
    RenderMirrorWorld();    // 수면 위에서만 Mirror 필요
    RenderWaterPlane();     // 먼저: 수면이 아래에 있으므로
    RenderFogFilter();
    RenderSkybox();
    RenderCloud();
}

// Post Effect
if (m_camera.IsUnderWater()) {
    RenderWaterFilter();    // 수중 전용 색 필터
}
m_postEffect.Bloom();
```

**수면 위 순서**: Mirror World → Water Plane → Fog → Sky → Cloud

- Mirror World를 먼저 렌더링하여 반사 텍스처를 준비
- Water Plane에서 반사 텍스처와 투영을 합성
- Fog/Sky/Cloud는 수면 위에 그려져 수면 뒤의 배경이 됨

**수중 순서**: Fog → Sky → Cloud → Water Plane

- 수중에서는 Mirror World 불필요 (반사 없음)
- Fog를 먼저 적용하여 수중 기하에 안개 효과
- Sky/Cloud를 그린 뒤 마지막에 Water Plane을 덮어 수면의 반투명 효과
- Post Effect에서 Water Filter로 전체 화면에 청색 필터

### 4.5 전체 파이프라인 요약

```
[수면 위]
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

(1) Mirror World (절반 해상도)
    Stencil Masking (수면 영역) → Mirror Sky → Mirror Cloud
    → Mirror Block (반전 카메라) → Blur (3회)
        ↓ mirrorWorldSRV

(2) Water Plane
    입력: originColor(기존 렌더), mirrorColor, positionTex
        ↓
    Beer-Lambert: projColor = lerp(origin, waterAlbedo, absorption)
        ↓
    Fresnel: blendColor = lerp(projColor×(1-F), mirrorColor, F)
        ↓ basicMSRTV에 출력


[수중]
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

(1) Fog Filter (가시거리 단축)
(2) Sky + Cloud
(3) Water Plane: lerp(origin, waterAlbedo, 0.5)
(4) Water Filter: lerp(render, filterColor × radianceWeight, strength)
    → 적응 시간에 따라 필터 강도/색상 변화
```

## 5. 문제점 & 해결

### 5.1 수면 가려짐 처리 (Mirror Masking)

**문제**: 수면이 불투명 블록 뒤에 있는 경우에도 반사가 그려지면 부자연스럽다.

**해결**: `MirrorMaskingPS`에서 G-Buffer의 Position 깊이와 수면 깊이를 비교하여, **블록에 가려진 수면 픽셀은 discard**한다. Stencil에 기록되지 않으므로 해당 영역에는 Mirror World가 렌더링되지 않는다.

### 5.2 Mirror World의 성능

**문제**: 전체 세계를 한 번 더 렌더링하는 것은 비용이 크다.

**해결**:
- **절반 해상도** (`MIRROR_WIDTH = APP_WIDTH/2`) — 반사 이미지는 Blur로 흐릿해지므로 절반 해상도로 충분
- **Stencil 마스킹** — 수면이 보이는 영역에만 렌더링하여, 화면 대부분이 수면이 아닌 경우 대폭 절약
- **Low-LOD 블록만 렌더링** (`RenderMirrorWorld`) — 반사에는 디테일이 불필요하므로 Low-LOD 메시 사용
- **수중에서는 Mirror 렌더링 생략** — 반사가 보이지 않으므로 완전히 건너뜀

### 5.3 수중 진입 시 급격한 시각 변화

**문제**: 수중에 진입하는 순간 화면이 갑자기 바뀌면 부자연스럽다.

**해결**: `m_waterAdaptationTime`으로 **시간 기반 적응**을 구현했다. 진입 직후에는 filterStrength가 0.9으로 높아 거의 앞이 안 보이지만, 2.5초에 걸쳐 0.4로 낮아지며 시야가 확보된다. Fog 거리도 함께 늘어나 점진적인 적응 효과를 만든다.

## 6. 결과

- **Fresnel 반사**: 수면을 비스듬히 보면 Mirror World의 반사가 강하게 보이고, 수직으로 내려다보면 수중 기하가 투명하게 비친다
- **Beer-Lambert 흡수**: 얕은 물에서는 바닥이 보이고, 깊은 물에서는 수면 자체 색상(바이옴별 기후 색)으로 불투명해진다
- **수면 애니메이션**: 32프레임 Still 텍스처 순환 + Normal Map으로 잔물결과 하이라이트가 표현된다
- **수중 적응**: 진입 직후 시야가 좁다가 점진적으로 확보되는 자연스러운 전환

## 7. 회고

- 현재 Planar Reflection은 **평면 수면(Y=64)** 에만 적용 가능하다. 폭포나 파도처럼 수면이 평면이 아닌 경우에는 SSR(Screen-Space Reflection)이나 Ray-Traced Reflection이 필요하다
- Mirror World에서 구름은 반전 카메라로 렌더링하지만, **인스턴스 오브젝트(풀, 꽃)** 의 반사는 구현 범위 밖이다. 추가하면 수면의 리얼리즘이 향상될 것이다
- 수중 Color Filter가 전체 화면에 균일하게 적용되는데, **깊이에 따라 필터 강도를 차등 적용**하면 (가까운 오브젝트는 덜 푸르게, 먼 오브젝트는 더 푸르게) 더 현실적인 수중 시야를 만들 수 있다
- Blur 3회로 수면 반사를 흐릿하게 만드는 대신, **수면 Normal Map의 왜곡**을 반사 텍스처 좌표에 적용하면 더 동적인 반사 왜곡이 가능하다
