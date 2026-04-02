# Fog Filter

<img width="1910" height="1054" alt="Image" src="https://github.com/user-attachments/assets/c132fdf8-faa1-470e-9af1-9f82172c2be4" />

<img width="1916" height="1070" alt="Image" src="https://github.com/user-attachments/assets/8b2713e5-9a6c-4829-a735-e6e8135c69da" />

<img width="1917" height="1080" alt="Image" src="https://github.com/user-attachments/assets/4417bd89-f3a8-4204-b93e-e2968680cb51" />

## 1. 개요

Deferred Shading 이후 Forward Pass에서 진행하는 Post Processing
Fill G-Buffer에서 사용된 Depth Buffer와 이미 렌더링된 버퍼를 SRV로 활용하여 샘플링 후 안개를 적용합니다.
Beer-Lambert 법칙 기반의 지수 감쇄 함수를 이용하여 블렌딩합니다.
안개색은 태양을 보는 방향에 관련된 함수를 사용합니다.

## 2. 핵심 아이디어

### 2.1 포스트 프로세싱 방식의 안개

안개를 각 오브젝트의 셰이더에 개별 적용하는 대신, Deferred Shading 결과물 전체에 한 번만 적용한다. 이를 위해 이미 완성된 화면(renderTex)과 깊이 버퍼(depthTex)를 입력으로 받는 풀스크린 쿼드 패스를 사용한다.

```
Deferred Shading 결과 (renderTex)  ─┐
                                    → FogFilterPS → 안개가 적용된 화면
Depth Buffer (depthTex)            ─┘
```

### 2.2 Beer-Lambert 지수 감쇄

안개 농도는 선형이 아닌 지수 감쇄를 따른다. 가까운 거리에서는 안개 효과가 거의 없다가 일정 거리를 넘으면 급격히 짙어지는데, 이는 실제 대기 산란의 Beer-Lambert 법칙과 유사하다.

```
float beerLambert = exp(-fogStrength * distFactor);
float fogFactor = 1.0 - beerLambert;
```

## 3. 셰이더 구현

### 3.1 입력 리소스

FogFilterPS.hlsl은 풀스크린 쿼드의 Pixel Shader로, 다음 3가지 입력을 받는다.

| 리소스              | 레지스터 | 타입                  | 내용                                |
| ------------------- | -------- | --------------------- | ----------------------------------- |
| `renderTex`         | t0       | `Texture2DMS<float4>` | Deferred Shading 결과 화면 (MSAA)   |
| `depthTex`          | t1       | `Texture2DMS<float>`  | Depth Buffer (MSAA)                 |
| `FogConstantBuffer` | b0       | cbuffer               | fogDistMin, fogDistMax, fogStrength |

renderTex는 `CopyResource()`로 현재 MSAA 렌더 타겟을 복사한 것이다. 원본에 직접 읽기/쓰기를 동시에 할 수 없으므로, 복사본을 SRV로 바인딩하고 원본 렌더 타겟에 다시 출력한다.

### 3.2 깊이에서 뷰 공간 거리 복원

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

### 3.3 안개 색 결정 (getFogColor)

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

| 조건           | dirWeight  | 안개 색              | 시각적 효과                |
| -------------- | ---------- | -------------------- | -------------------------- |
| 태양 반대 방향 | 0에 가까움 | `normalHorizonColor` | 차가운 톤의 수평선 색      |
| 태양 방향      | 1에 가까움 | `sunHorizonColor`    | 따뜻한 톤의 태양빛 산란 색 |

`dot(lightDir, eyeDir)`은 순방향 산란(forward scattering)을 근사한다.
실제 대기에서 태양 방향을 바라보면 빛이 입자를 통과하며 산란되어 따뜻한 색조를 띠고, 반대 방향은 차가운 색조를 띤다.
이 방향별 색상 차이가 일출/일몰 시 안개에 자연스러운 그라데이션을 만든다.

`normalHorizonColor`와 `sunHorizonColor`는 Global Constant Buffer에서 전달되며, Skybox의 수평선 색과 동일한 값을 사용하여 안개가 하늘과 자연스럽게 연결된다.

**수중에서의 안개 색:**

수중에서는 `normalZenithColor`(천정색)로 고정된다. 물속에서는 태양 방향에 따른 산란 차이가 거의 없고, 전체적으로 균일한 푸른 빛이 지배적이기 때문이다.

### 3.4 안개 강도 계산 (getFogFactor)

```hlsl
float getFogFactor(float3 pos)
{
    //Beer-Lambert law
    float dist = length(pos.xyz);

    float distFactor = saturate((dist - fogDistMin) / (fogDistMax - fogDistMin));
    float beerLambert = exp(-fogStrength * distFactor);

    return 1.0 - beerLambert;
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

### 3.5 최종 색상 합성

```hlsl
float fogFactor = getFogFactor(viewPos);
float3 blendColor = lerp(renderColor, fogColor, fogFactor);
```

## 4. 환경 파라미터

안개의 거리 범위와 강도는 지상과 수중에서 크게 다르게 설정된다.

### 4.1 지상 안개

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

### 4.2 수중 안개

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

## 6. 회고

- DepthBuffer와 Render결과를 이용한 간단한 Fog
- Beer-Lambert를 활용한 Fog로 결과적으로는 만족함
- UnderWater의 경우가 존재해서 MSAA 풀스크린 후처리라는 점이 아쉬움
