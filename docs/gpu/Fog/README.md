# Fog Rendering

## 1. 개요

Fog는 Deferred Shading 이후 Forward Pass에서 풀스크린 포스트 프로세싱으로 적용되는 거리 안개다. 이미 렌더링된 화면 색상과 Depth Buffer를 읽어, 카메라에서 먼 픽셀일수록 안개 색으로 대체하는 방식이다. Beer-Lambert 법칙 기반의 지수 감쇄 함수로 블렌딩 비율을 결정하며, 안개 색은 태양 방향에 따라 동적으로 변한다.

## 2. 도입 동기

복셀 월드에서 렌더 거리(MAX_RENDER_DISTANCE=320)를 넘으면 청크가 갑자기 사라져 시각적으로 부자연스럽다. 거리 안개를 적용하면 먼 오브젝트가 하늘 색에 녹아들며 자연스럽게 사라지고, 렌더 거리 한계를 시각적으로 감출 수 있다. 추가로 수중 환경에서는 안개 거리를 극단적으로 줄여 시야 제한 효과를 구현한다.

## 3. 핵심 아이디어

### 3.1 포스트 프로세싱 방식의 안개

안개를 각 오브젝트의 셰이더에 개별 적용하는 대신, Deferred Shading 결과물 전체에 한 번만 적용한다. 이를 위해 이미 완성된 화면(renderTex)과 깊이 버퍼(depthTex)를 입력으로 받는 풀스크린 쿼드 패스를 사용한다.

```
Deferred Shading 결과 (renderTex)  ─┐
                                     ├─→ FogFilterPS → 안개가 적용된 화면
Depth Buffer (depthTex)            ─┘
```

이 방식의 장점은 모든 오브젝트에 균일한 안개가 적용되며, 셰이더 코드가 한 곳에 집중되어 유지보수가 간단하다는 것이다.

### 3.2 Beer-Lambert 지수 감쇄

안개 농도는 선형이 아닌 지수 감쇄를 따른다. 가까운 거리에서는 안개 효과가 거의 없다가 일정 거리를 넘으면 급격히 짙어지는데, 이는 실제 대기 산란의 Beer-Lambert 법칙과 유사하다.

```
fogFactor = exp(-strength * normalizedDist)
```

선형 감쇄 대비 자연스러운 전환 곡선을 제공하며, `strength` 값 하나로 안개의 밀도를 직관적으로 제어할 수 있다.

## 4. 셰이더 구현

### 4.1 입력 리소스

FogFilterPS.hlsl은 풀스크린 쿼드의 Pixel Shader로, 다음 3가지 입력을 받는다.

| 리소스 | 레지스터 | 타입 | 내용 |
|--------|----------|------|------|
| `renderTex` | t0 | `Texture2DMS<float4>` | Deferred Shading 결과 화면 (MSAA) |
| `depthTex` | t1 | `Texture2DMS<float>` | Depth Buffer (MSAA) |
| `FogConstantBuffer` | b0 | cbuffer | fogDistMin, fogDistMax, fogStrength |

renderTex는 `CopyResource()`로 현재 MSAA 렌더 타겟을 복사한 것이다. 원본에 직접 읽기/쓰기를 동시에 할 수 없으므로, 복사본을 SRV로 바인딩하고 원본 렌더 타겟에 다시 출력한다.

### 4.2 깊이에서 뷰 공간 거리 복원

```hlsl
float depth = depthTex.Load(input.posProj.xy, sampleIndex).r;
float3 viewPos = texcoordToViewPos(input.texcoord, depth);
```

화면의 각 픽셀에서 Depth Buffer 값을 읽고, 이를 뷰 공간 좌표로 역변환한다.

**texcoordToViewPos 과정:**

```hlsl
// 1. 텍스처 좌표(0~1) → NDC(-1~1)
posProj.xy = texcoord * 2.0 - 1.0;
posProj.y *= -1;            // DirectX UV 좌표계 보정
posProj.z = projDepth;      // 깊이값 그대로 사용
posProj.w = 1.0;

// 2. NDC → 뷰 공간 (역투영)
posView = mul(posProj, invProj);
posView.xyz /= posView.w;   // 원근 나눗셈 역연산
```

결과물인 `viewPos`는 카메라 원점(0,0,0) 기준의 뷰 공간 좌표이므로, `length(viewPos)`가 곧 카메라~픽셀 간 거리가 된다. 월드 공간이 아닌 뷰 공간 거리를 사용하는 이유는, 안개는 카메라로부터의 절대 거리에 의존하므로 뷰 공간에서 원점까지의 거리가 가장 직접적인 계산이기 때문이다.

### 4.3 안개 색 결정 (getFogColor)

```hlsl
float3 getFogColor(float3 lightDir, float3 eyeDir)
{
    float dirWeight = max(dot(lightDir, eyeDir), 0.0);
    float3 fogColor = lerp(normalHorizonColor, sunHorizonColor, dirWeight);

    if (isUnderWater)
        fogColor = normalZenithColor;

    return fogColor;
}
```

안개 색은 고정값이 아니라 시선 방향과 태양 방향의 관계에 따라 실시간으로 변한다.

**지상에서의 안개 색:**

| 조건 | dirWeight | 안개 색 | 시각적 효과 |
|------|-----------|---------|-------------|
| 태양 반대 방향 | 0에 가까움 | `normalHorizonColor` | 차가운 톤의 수평선 색 |
| 태양 방향 | 1에 가까움 | `sunHorizonColor` | 따뜻한 톤의 태양빛 산란 색 |

`dot(lightDir, eyeDir)`은 순방향 산란(forward scattering)을 근사한다. 실제 대기에서 태양 방향을 바라보면 빛이 입자를 통과하며 산란되어 따뜻한 색조를 띠고, 반대 방향은 차가운 색조를 띤다. 이 방향별 색상 차이가 일출/일몰 시 안개에 자연스러운 그라데이션을 만든다.

`normalHorizonColor`와 `sunHorizonColor`는 Global Constant Buffer에서 전달되며, Skybox의 수평선 색과 동일한 값을 사용하여 안개가 하늘과 자연스럽게 연결된다.

**수중에서의 안개 색:**

수중에서는 `normalZenithColor`(천정색)로 고정된다. 물속에서는 태양 방향에 따른 산란 차이가 거의 없고, 전체적으로 균일한 푸른 빛이 지배적이기 때문이다.

### 4.4 안개 강도 계산 (getFogFactor)

```hlsl
float getFogFactor(float3 pos)
{
    // Beer-Lambert law
    float dist = length(pos.xyz);
    float distFog = saturate((dist - fogDistMin) / (fogDistMax - fogDistMin));
    float fogFactor = exp(-fogStrength * distFog);
    return fogFactor;
}
```

거리를 정규화한 뒤 지수 감쇄 함수에 넣어 안개 강도를 산출한다.

**단계별 분해:**

```
[1] dist = length(viewPos)
    → 카메라에서 해당 픽셀까지의 뷰 공간 거리 (절대값)

[2] distFog = saturate((dist - fogDistMin) / (fogDistMax - fogDistMin))
    → fogDistMin 이하: 0.0 (안개 없음)
    → fogDistMin ~ fogDistMax: 0.0 ~ 1.0 (선형 정규화)
    → fogDistMax 이상: 1.0 (최대 안개)

[3] fogFactor = exp(-fogStrength * distFog)
    → distFog=0 일 때: exp(0) = 1.0 (원본 색 100%)
    → distFog=1 일 때: exp(-strength) (안개 최대)
```

`fogFactor`는 "원본 색상을 얼마나 유지할 것인가"를 나타내는 값이다.

**fogStrength에 따른 감쇄 곡선:**

```
fogFactor
1.0 ┤━━━━━╲
    │      ╲╲  strength=2 (지상)
    │        ╲╲
    │          ╲━━━━━
    │   ╲
0.0 ┤    ╲ strength=5 (수중)
    └──────────────── distFog
    0.0              1.0
```

| 환경 | fogStrength | exp(-strength) | 의미 |
|------|-------------|----------------|------|
| 지상 | 2.0 | 0.135 | 최대 거리에서 원본 색 13.5% 잔존 |
| 수중 (초기) | 5.0 | 0.007 | 최대 거리에서 원본 색 거의 소멸 |
| 수중 (적응 후) | 4.0 | 0.018 | 적응 완료 시 약간 완화 |

### 4.5 최종 색상 합성

```hlsl
float3 renderColor = renderTex.Load(input.posProj.xy, sampleIndex).rgb;
float3 blendColor = lerp(fogColor, renderColor, fogFactor);
return float4(blendColor, 1.0);
```

`lerp(fogColor, renderColor, fogFactor)`는 fogFactor에 따라 안개 색과 원본 색을 보간한다.

```
blendColor = fogColor × (1 - fogFactor) + renderColor × fogFactor
```

| fogFactor | 결과 | 상황 |
|-----------|------|------|
| 1.0 | renderColor (원본) | 가까운 픽셀 - 안개 없음 |
| 0.5 | 50:50 혼합 | 중간 거리 |
| 0.0 | fogColor (안개) | 먼 픽셀 - 완전히 안개에 덮임 |

MSAA 텍스처를 사용하므로 `SV_SampleIndex`로 서브샘플별로 처리하여 엣지 부분의 안개 품질도 유지된다.

## 5. 환경별 파라미터

안개의 거리 범위와 강도는 지상과 수중에서 크게 다르게 설정된다.

### 5.1 지상 안개

```
fogDistMin  = LOD_RENDER_DISTANCE  = 260
fogDistMax  = MAX_RENDER_DISTANCE  = 320
fogStrength = 2.0
```

```
0          260        320
├──────────┤──────────┤
  안개 없음   서서히 짙어짐
             (60블록 구간)
```

LOD 전환 거리(260)부터 안개가 시작되어 최대 렌더 거리(320)에서 거의 사라진다. 이 60블록 구간이 청크 언로딩 경계를 자연스럽게 가린다.

### 5.2 수중 안개

수중에서는 안개가 극단적으로 가까워지며, 시간이 지남에 따라 "눈 적응" 효과로 시야가 서서히 넓어진다.

```
입수 직후 (duration=0):    fogDistMin=15,  fogDistMax=30,   fogStrength=5.0
적응 완료 (duration=1):    fogDistMin=30,  fogDistMax=120,  fogStrength=4.0
적응 시간:                 m_waterMaxDuration = 2.5초
```

```
[입수 직후]
0    15   30
├────┤────┤
      짙은 안개 (15블록 시야)

[2.5초 후 적응 완료]
0         30              120
├─────────┤───────────────┤
           서서히 짙어짐 (90블록 시야)
```

duration은 `m_waterAdaptationTime / m_waterMaxDuration`으로 0→1 범위를 갖는다. 입수 시 0에서 시작하여 2.5초에 걸쳐 1.0에 도달하며, 수면 위로 나오면 즉시 0으로 리셋된다.

## 6. 렌더링 흐름

### 6.1 렌더 순서에서의 위치

```
[지상]
Deferred Shading → ConvertToMSAA → Mirror → Water → FogFilter → Skybox → Cloud

[수중]
Deferred Shading → ConvertToMSAA → FogFilter → Skybox → Cloud → Water
```

지상에서 Fog는 스카이박스와 구름보다 **먼저** 적용된다. 이는 Fog가 Deferred Shading 결과(불투명 오브젝트)에만 적용되고, 이후 그려지는 스카이박스와 구름은 안개의 영향을 받지 않도록 하기 위함이다. 스카이박스는 하늘 그 자체이므로 안개에 가려질 필요가 없고, 구름은 자체적으로 거리 기반 투명 처리를 수행한다.

### 6.2 GPU 리소스 흐름

```
basicMSBuffer (현재 MSAA 렌더 결과)
     │
     ├─ CopyResource → copyForwardRenderBuffer → copyForwardSRV (t0)
     │
     ├─ basicDepthSRV (t1)
     │
     └─ FogFilterPS 실행 → basicMSRTV에 출력 (원본 덮어쓰기)
```

동일 버퍼에 동시 읽기/쓰기가 불가능하므로 `CopyResource()`로 복사본을 만든다. Fog 결과는 원본 MSAA 렌더 타겟(`basicMSRTV`)에 직접 출력되어, 이후 Forward Pass(스카이박스, 구름)에서 이 버퍼 위에 그려진다.

## 7. 회고

- Beer-Lambert 감쇄가 단순 선형 보간보다 자연스럽지만, 현재 높이(Y)를 고려하지 않는다. 높이 기반 밀도 변화를 추가하면 산 위에서 안개가 옅어지고 계곡에서 짙어지는 효과를 낼 수 있다.
- 안개 색을 스카이박스와 동일한 horizon color로 사용하여 하늘과의 연결은 자연스럽지만, 시간대별로 안개 자체의 색상을 분리 제어하면 더 풍부한 분위기를 연출할 수 있다.
- 수중 적응 효과가 선형으로 증가하는데, 실제 눈 적응은 초반에 빠르고 후반에 느려지므로 이징 곡선을 적용하면 더 자연스러울 수 있다.
