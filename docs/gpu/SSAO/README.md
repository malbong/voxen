# SSAO (Screen-Space Ambient Occlusion)

## 1. 개요

Voxen에서는 Deferred Shading 파이프라인의 일부로, G-Buffer의 Position/Normal 정보를 활용하여 **View Space**에서 반구 샘플링 기반 SSAO를 계산한다.

Block들과 Instance에 적절한 occlusion 를 Screen Space에서 생성한다.

## 2. 도입 동기

원래의 Minecraft 형식의 Voxel에서든 인접한 블록의 경우의 수를 두고 AO 레벨을 Vertex Data에 옮겨서 사용한다.

현재 코드에서도 8byte Vertex Data에 `미사용 3비트`가 존재하여 구현할 수 있었다.

```
Bit:  31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
      [  미사용  3비트  ] [    X 6비트    ] [    Y 6비트    ] [    Z 6비트    ] [F 3] [  TexIndex 8비트  ]
```

이 방식을 사용할 수 있지만, 렌더러를 구현하는 입장에서 SSAO를 직접 구현해보고 싶었다.

이 SSAO를 하기 위해 G-Buffer가 필요했고, Forward Rendering 단계를 Deferred Pass와 Forward Pass를 구분하여 파이프라인을 재정리하게 되는 계기가 되었다.

## 3. 핵심 아이디어

### 3.1 SSAO의 원리

0. 반구 형태의 무작위 샘플 포인트를 배치하고, 반구 자체를 회전 시킬 `Tangent Random Vector`도 배치한다.
1. 픽셀의 `View Position`과 `View Normal`을 가져온다.
2. `View Normal`과 `Tangent Random Vector`를 기준으로 TBN Matrix를 구성하여 샘플 포인트를 View 좌표의 offset으로 변환한다.
3. `View Position`에 offset을 더한 좌표(`Sample Position`)를 화면에 투영한다.
4. 투영된 값을 텍스쳐 좌표로 활용하여 원래 G-Buffer에 저장되어 있는 값과 Depth를 비교한다.
5. 저장되어 있는 값보다 샘플 위치가 멀리 뒤에 있으면 가려지는 부분이므로 Occlusion 값을 증가시킨다.
6. 이러한 과정을 Sample(16)만큼 반복하여 비율을 결정한다.

### 3.2 View Space에서 연산 수행

- **깊이 비교의 단순화**: View Space에서 Z값은 카메라로부터의 거리이므로, "앞/뒤" 판정이 Z 비교 하나로 가능하다.
- **투영 후 스크린 혹은 텍스쳐 좌표 변환이 용이**: View Space 샘플 포인트를 Projection Matrix로 곱하면 바로 텍스처 좌표를 얻을 수 있다.
- **View Space 기준으로 상대적인 값이 작음**: World 기준이 아닌, View Space기준이라 연산에 안정적이다.

### 3.3 Edge Preserved Blur

- SSAO만 렌더링 하는 경우 노이즈가 눈에 보여 Blur처리를 해야한다.
- 단순히 Gaussian Blur로 처리하는 경우 차폐의 정도가 Edge 부분에서 급격히 값이 섞이게 되어 어색한 결과가 나온다
- Bilateral Blur를 사용하여 Edge를 살린다.

## 4. 구현 내용

### 4.1 CPU: 샘플 커널과 노이즈 생성 (SSAO.cpp)

#### 샘플 커널 (sampleKernel[16]) 구성

```cpp
// Initialize()
for (UINT i = 0; i < KERNEL_COUNT; ++i) {
	Vector4 sampleKernel;

	sampleKernel.x = randomFloats(generator) * 2.0f - 1.0f;
	sampleKernel.y = randomFloats(generator) * 2.0f - 1.0f;
	sampleKernel.z = randomFloats(generator); // hemisphere
	sampleKernel.w = 0.0f;
	sampleKernel.Normalize();

	sampleKernel *= randomFloats(generator); // Random Scaling

	float scale = (float)i / KERNEL_COUNT;
	sampleKernel *= Utils::Lerp(0.1f, 1.0f, scale * scale); // 점진적 Scaling
    ...
}
```

`sampleKernel.z` 값은 [0, 1]로 구성하는데, 이후에 TBN Matrix에 곱해지는 경우 법선 방향으로 증가한다.

#### 회전 노이즈 (rotationNoise[16])

```cpp
for (int i = 0; i < 16; ++i) {
    Vector4 rotationNoise;
    rotationNoise.x = randomFloats(generator) * 2.0f - 1.0f;
    rotationNoise.y = randomFloats(generator) * 2.0f - 1.0f;
    rotationNoise.z = randomFloats(generator) * 2.0f - 1.0f;
    rotationNoise.Normalize();
}
```

16개의 정규화된 랜덤 방향 벡터를 4×4 그리드로 배열한다. 이 벡터들은 셰이더에서 **Gram-Schmidt 과정**을 통해 법선 주위의 Tangent Vector가 된다.

`rotationNoise.xyz` 값은 `[-1, 1]`로 구성되는데 법선의 방향을 현재상태에서 모르기 때문에 반구의 형태가 아닌 구의 형태로 구성한다.

### 4.2 GPU: 회전 노이즈 인덱싱 (SsaoPS.hlsl)

**16개의 회전 노이즈 텍스처를 타일링**하는 것이 첫 번째다.

픽셀 스크린 좌표를 `% 4` 연산으로 인덱싱하여 4x4 픽셀에 `rotationNoise[16]`을 모두 사용한다.

```hlsl
uint ix = uint(screenPos.x) % 4;
uint iy = uint(screenPos.y) % 4;
float3 randomVec = normalize(rotationNoise[ix + 4 * iy].xyz);
```

### 4.3 ViewSpace를 기준으로한 TBN Matrix 구성

```hlsl
float3 T = normalize(randomVec - viewNormal * dot(viewNormal, randomVec)); // randomVec에서 Normal방향의 수직성분을 제거
float3 B = cross(viewNormal, T);
float3x3 TBN = float3x3(T, B, viewNormal);
```

랜덤 벡터를 법선 평면에 투영하여 Tangent를 구하는 **Gram-Schmidt 직교화**다.

TBN Matrix의 Basis(각 T,B,N 벡터)가 View Space 기준으로 형성된 TBN Matrix이므로 `TBN_Space좌표 * TBNMatrix => ViewSpace좌표`가 될 것이다.

### 4.4 반구 샘플링과 차폐 판정

```hlsl
 float occlusionFactor = 0.0;
 float radius = 1.5;
 float bias = 0.05;

 uint validSampleCount = 0;
 const float INVALID_POSITION = -1.0;
 const uint SSAO_SAMPLE_COUNT = 16;
 [unroll]
 for (uint i = 0; i < SSAO_SAMPLE_COUNT; ++i)
 {
    // 1. 샘플 위치 TBN Matrix 계산 (View Space)
    float3 sampleOffset = mul(sampleKernel[i].xyz, TBN);
    float3 samplePos = viewPos + sampleOffset * radius; // samplePos of viewspace

    // 2. NDC 좌표 투영
    float4 sampleProjPos = float4(samplePos, 1.0);
    sampleProjPos = mul(sampleProjPos, proj);
    sampleProjPos.xyz /= sampleProjPos.w; // [-1, 1]

    // 3. NDC -> Texcoord 연산
    float2 sampleTexcoord = sampleProjPos.xy;
    sampleTexcoord.x = saturate(sampleTexcoord.x * 0.5 + 0.5); // [-1, 1] -> [0, 1]
    sampleTexcoord.y = saturate(-(sampleTexcoord.y * 0.5) + 0.5); // [-1, 1] -> [1, 0]

    // 4. Texcoord -> Screencoord : Texture2DMS를 사용하기에 Screen coord 변경
    float2 sampleScreenCoord = texcoordToScreen(sampleTexcoord, appWidth, appHeight);
    float4 position = positionTex.Load(sampleScreenCoord, 0); // SampleIndex 중 아무거나 하나 집어도 무관: 샘플의 위치가 다르다고 가정하면 됨
    float4 storedViewPos = mul(float4(position.xyz, 1.0), view);
    if (position.w == INVALID_POSITION)
        storedViewPos.z = 1000.0; // 기하정보가 없는 경우 체크

    // 5. range Weight 설정
    float diff = max(1e-4, length(viewPos - storedViewPos.xyz));
    float w = smoothstep(0.0, 1.0, radius / diff);
    float rangeWeight = pow(w, 2.0);

    // 6. Depth 검사
    // 저장되어 있는 값이 더 가까운 경우 차폐가 생김
    // 동일한 위치인 경우 저장되어 있는 값을 뒤로 밀어 차폐가 생기지 않게함 -> bias 더함
    occlusionFactor += (storedViewPos.z + bias < samplePos.z ? 1.0 : 0.0) * rangeWeight;
    validSampleCount++;
 }

 return occlusionFactor / float(validSampleCount);
```

샘플 위치를 TBN 연산으로 ViewSpace로 변환한다.

그것을 저장되어 있는 텍스쳐에 샘플링할 수 있도록 좌표계 변환을 진행한다.

이후 Depth를 비교하기 전 Range 검사를 진행한다.

- Range Weight를 활용하는 이유: 멀리 떨어져있는 메쉬끼리는 서로 간접광에 미치는 영향이 별로 없기 때문이다.

Depth 비교 시에는 `bias`를 활용하여 비슷한 Depth나 같은 표면에 있는 곳에는 차폐가 생기지 않도록 한다.

### 4.5 거리 감쇠(attenuation)와 최종 출력

```hlsl
float occlusionFactor = getOcclusionFactor(input.posProj.xy, viewPos, viewNormal);

float maxSSAODistance = CHUNK_SIZE * 3;
float minSSAODistance = CHUNK_SIZE;
float distance = length(viewPos.xyz);
float attenuation = saturate((maxSSAODistance - distance) / (maxSSAODistance - minSSAODistance));

return (occlusionFactor * attenuation);
```

- 먼 거리의 SSAO는 해상도 한계로 부정확해지므로, 이를 방지하기 위해 사용된다.
- `minSSAODistance(32.0)` 내부 거리에서는 SSAO 효과를 확실히 하고 `maxSSAODistance(96.0)` 이후부터는 나타나지 않게 조정한다.
- 차폐가 있으면 값이 `1.0` 없으면 값이 `0.0`이다. 이는 라이팅 연산에 활용할 때는 뒤집어 사용한다. `1.0 - ao`

### 4.6 MSAA Edge 처리 (`mainMSAA` 분기)

`main`에서는 단순히 Non-Edge Pixel이라 한번만 처리했으면 됐으나, Edge 픽셀에서는 `mainMSAA`함수로 분기하여 4개 MSAA 샘플 각각에 대해 SSAO를 계산해야 한다.

이 때, G-Buffer 과정 중에 생성한 `SV_Coverage` 값을 활용하여 SampleWeight를 두고 좀 더 최적화가 가능하다.

하지만 단순히 `SV_Coverage`값을 기준으로 SampleWeight 연산을 하게되는 경우 잎사귀와 같이 `SemiAlpha`가 Clip된 부분의 영역에서 문제가 발생한다.

이로써, `SemiAlpha가` 없는 Pixel의 경우는 SampleWeight를 활용하여 반복 횟수를 줄이고, `SemiAlpha가` 존재하는 경우 단순히 `SAMPLE_COUNT` 만큼 SSAO 검사 반복문을 진행한다.

#### semiAlphaCount 검사 -> 마스킹값 체크

G-Buffer에서 Alpha-Clip 기하가 있는 샘플은 `normalEdgeTex.w = 2.0`으로 마킹된다. 이 값을 읽어 픽셀 내 Alpha-Clip 샘플 수를 파악한다.

```hlsl
const float SEMIALPHA_MASK = 2.0;
uint semiAlphaCount = 0;
for (uint s = 0; s < SAMPLE_COUNT; ++s)
{
    float ne_w = normalEdgeTex.Load(input.posProj.xy, s).w;
    if (ne_w == SEMIALPHA_MASK)
        ++semiAlphaCount;
}
```

#### coverageAnalysis — SV_Coverage 기반 중복 샘플 병합

`coverageTex`에는 G-Buffer 채우기 패스의 `SV_Coverage` 값이 저장되어 있다.

Coverage 값이 동일한 두 샘플은 **동일한 기하를 덮고 있음**을 의미하므로(실제로는 다른 메쉬가 섞일 순 있음), 대표 샘플의 weight를 증가시키고 나머지를 0으로 처리하여 skip한다.

예를 들어 4샘플 중 3개가 동일 표면을 덮는 경우:

```
sample 0: weight 3  ← 대표, getOcclusionFactor 1회 계산 후 3의 가중치를 곱해서 사용
sample 1: weight 0  ← skip
sample 2: weight 0  ← skip
sample 3: weight 1  ← 다른 표면, getOcclusionFactor 1회 계산
→ getOcclusionFactor 2회 (vs 미사용 시 4회)

uint4 coverageAnalysis(uint4 coverage)
{
    uint4 sampleWeight = uint4(1, 1, 1, 1);

    if (coverage.x == coverage.y) { ++sampleWeight.x; coverage.y = 0; }
    if (coverage.x == coverage.z) { ++sampleWeight.x; coverage.z = 0; }
    if (coverage.x == coverage.w) { ++sampleWeight.x; coverage.w = 0; }
    if (coverage.y == coverage.z) { ++sampleWeight.y; coverage.z = 0; }
    if (coverage.y == coverage.w) { ++sampleWeight.y; coverage.w = 0; }
    if (coverage.z == coverage.w) { ++sampleWeight.z; coverage.w = 0; }

    sampleWeight.x = (coverage.x > 0) ? sampleWeight.x : 0;
    ...
    return sampleWeight;
}
```

#### sampleWeight 활용

```hlsl
if (semiAlphaCount == 0)
{
    uint4 coverage;
    uint4 sampleWeight;

    coverage.x = coverageTex.Load(input.posProj.xy, 0);
    coverage.y = coverageTex.Load(input.posProj.xy, 1);
    coverage.z = coverageTex.Load(input.posProj.xy, 2);
    coverage.w = coverageTex.Load(input.posProj.xy, 3);

    sampleWeight = coverageAnalysis(coverage);
    sampleWeightArray[0] = sampleWeight.x;
    sampleWeightArray[1] = sampleWeight.y;
    sampleWeightArray[2] = sampleWeight.z;
    sampleWeightArray[3] = sampleWeight.w;
}
```

- **semiAlphaCount == 0**: Alpha-Clip 기하 없음 → Coverage 값이 표면 동일성의 신뢰할 수 있는 기준이 되므로 `coverageAnalysis` 적용
- **semiAlphaCount > 0**: Alpha-Clip 기하 존재 → Coverage 마스크가 클립 여부를 반영하여 표면 동일성 판단에 사용할 수 없으므로 모든 샘플을 독립적으로 계산

#### 결과: SampleWeight 없이 4회 반복 VS SampleWeight 활용하여 반복(현재코드) - RenderDoc Duration

SSAO 패스 단독 기준 **110μs → 100μs** (약 9% 개선).

단, SSAO의 `mainMSAA`는 전체 화면 중 Edge 픽셀에서만 실행되므로 전체 프레임 렌더링 시간에 대한 기여는 사실 미미하다.

구현의 복잡도와 결과를 비교하여 생각하면 단순히 4회 반복으로도 충분하나, SampleWeight를 활용한 쉐이더 구현 실습을 해보고 싶었다.

### 4.7 Edge-Preserving Blur (BlurBilateralPS.hlsl)

SSAO 결과의 노이즈를 제거하되, 경계를 보존하기 위해 **Bilateral Filter** 를 사용한다.

Voxel 특성상 Edge가 많고, 이 경계에서 섞이지 않아야 한다.

섞이는 경우 다음과 같이 잎사귀 근처에서 차폐가 섞인다. 그래서 **Bilateral Filter**를 사용하되 입맛에 맞춰 변경한다.

<img width="950" height="550" alt="Image" src="https://github.com/user-attachments/assets/565890e3-0d8c-4bda-9139-43a9b7c4bb19" />

<img width="950" height="550" alt="Image" src="https://github.com/user-attachments/assets/ad703081-80f0-46ef-833e-92c14ea1d7a7" />

#### 핵심 설계

```hlsl
float4 base = renderTex.Sample(linearClampSS, input.texcoord);
float sigma = 0.325;

sumColor  += base;
sumWeight += 1.0;

[unroll]
for (int i = -2; i <= 2; ++i)
{
    for (int j = -2; j <= 2; ++j)
    {
        if (i == 0 && j == 0)
            continue;

        float2 offset = float2(dx * i, dy * j);
        float4 s = renderTex.Sample(linearClampSS, input.texcoord + offset);

        float diff = length(base - s);
        float w = exp(-diff * diff / (sigma * sigma)) * pow(s, 1.25);

        sumColor  += s * w;
        sumWeight += w;
    }
}
```

두 가지 weight를 사용한다:

- **Range weight** `exp(-diff²/σ²)`
  - SSAO 값 차이가 클수록(=경계) weight를 0으로 수렴시켜 섞지 않는다. σ가 작을수록 경계 차단이 예민해진다

- **Occlusion weight** `pow(s, 1.25)`
  - Range weight만으로는 해결되지 않는 문제를 보완한다.
  - Alpha-Clip 잎사귀 경계처럼 차폐가 있는 픽셀(s≈1.0)과 차폐가 없는 빈 픽셀(s≈0.0)이 인접할 때, Range weight가 충분히 차단하더라도 낮은 차폐 샘플이 조금씩 섞이면 occluded 영역의 차폐값이 섞인다.
  - Occlusion weight는 샘플 자체의 차폐 정도를 가중치로 부여하여, 차폐가 낮은 샘플(빈 공간, Alpha-Clip 구멍)의 영향력을 떨어뜨려 퍼지지 않게한다.

이 때, 주변의 샘플을 5x5만큼 샘플하는데 이유는 다음과 같다.

```hlsl
// SsaoPS.hlsl
// 4x4px -> same random vector
// 4x4 pixel 마다 주기 반복
uint ix = uint(screenPos.x) % 4;
uint iy = uint(screenPos.y) % 4;
float3 randomVec = normalize(rotationNoise[ix + 4 * iy].xyz);
```

- 주변 3x3 샘플을 한다면 반복되는 4x4픽셀에서의 Rotation Random Vector를 올바르게 섞지 못해 노이즈 보여 부드럽지 못했다.
- 비용이 조금 더 들지만 5x5 샘플로 부드럽게 Blur 처리를 할 수 있었다.

### 5. 전체 파이프라인

```
[CPU - PostEffect.cpp]
sampleKernel[16]: 반구 내 랜덤 방향, 가까울수록 밀집
rotationNoise[16]: 4×4 랜덤 회전 벡터
        ↓ Constant Buffer

[GPU - SsaoPS.hlsl::getOcclusionFactor()]
screenPos → % 4 → 4×4 노이즈 인덱스
randomVec → Gram-Schmidt → TBN (법선 방향 반구)
        ↓
16 samples × (TBN 변환 → View Space offset → 화면 투영 샘플 → RangeCheck → 깊이 비교)
        ↓
occlusionFactor / 16 → 거리 감쇠 → 차폐값 저장
        ↓ BlurBilateralPS (5×5, range weight + occlusion weight)
ssaoTex → ShadingBasicPS에서 활용
```

## 6. 문제점 & 해결

### 6.1 Alpha-Clip 경계에서의 Blur Artifact

**문제**: 잎사귀처럼 Alpha Clip된 메시의 경계에서, 실제 차폐가 있는 픽셀(SSAO≈1.0)과 Alpha Clip으로 비어있는 픽셀(SSAO≈0.0)이 인접한다. 일반 Gaussian Blur는 이 둘을 평균내어 경계에 0.5 중간값을 생성한다. 결과적으로 잎사귀 주변에 흐릿한 테두리(Half-Occlusion Ring)가 나타난다.

초기에는 **Bloom 기반 스무딩**으로 해결했다. Bloom의 down/up sampling은 단순 평균이 아니라 밝은(occluded) 값을 주변으로 확장하는 경향이 있어, 0.5 중간값 생성 없이 차폐 영역을 자연스럽게 퍼뜨린다. 그러나 SSAO 노이즈가 그대로 누수되어 시각적으로 두드러지는 문제가 있었다.

**해결**: Range weight와 Occlusion weight를 결합한 Bilateral Filter. 0.0↔1.0 경계에서 range weight가 0에 수렴하여 섞지 않으며, occlusion weight가 낮은 차폐 샘플(빈 공간)의 영향력을 억제한다.

### 6.2 Alpha-Clip 경계에서의 SV_Coverage 신뢰성 문제

**문제**: Coverage 값이 동일한 두 샘플을 "동일 기하"로 판단하는 `coverageAnalysis`는, Alpha-Clip 기하가 있는 픽셀에서 신뢰할 수 없다. Alpha-Clip은 서브픽셀 단위로 클립을 적용하므로 동일한 Coverage 마스크를 가지더라도 실제로는 서로 다른 표면(클립된 잎사귀 / 뒤의 배경)일 수 있다. 이 경우 `coverageAnalysis`가 다른 표면을 동일 기하로 묶어 weight를 부여하면 잘못된 occlusionFactor가 계산된다.

**해결**: G-Buffer 채우기 패스에서 Alpha-Clip 기하 샘플에 `normalEdgeTex.w = 2.0`으로 마킹한다. `mainMSAA`에서 `semiAlphaCount`를 검사하여, Alpha-Clip 샘플이 하나라도 있으면 `coverageAnalysis`를 건너뛰고 모든 샘플을 weight = 1로 독립 계산한다.

### 6.3 mainMSAA 분기에 대한 트레이드 오프

**비교**: SemiAlpha Count Check + SampleWeight의 비용과 그에 따른 반복 횟수 감소 **VS** 단순히 조건 없이 4회 반복

**해결**: 전자로 선택, 구현 복잡도는 늘어나지만 실질적으로 미비하지만 빨라지고, 구현의 연습의 계기.

### 6.4 먼 거리의 SSAO Flickering

**문제**: 먼 거리의 오브젝트의 경우 부정확한 값으로 인해 SSAO가 카메라 움직임에 따라 크게 흔들린다.

**해결**: SSAO를 거리 따라 적용하여 감쇄시켜 먼 거리에 있는 오브젝트에 대한 차폐를 생기지 않도록한다.

```
float maxSSAODistance = CHUNK_SIZE * 3;
float minSSAODistance = CHUNK_SIZE;
float distance = length(viewPos.xyz);
float attenuation = saturate((maxSSAODistance - distance) / (maxSSAODistance - minSSAODistance));

return (occlusionFactor * attenuation);
```

## 7. 회고

- 처음으로 해보는 G-Buffer를 활용한 Screen Space 로직이였다.
  - https://learnopengl.com/Advanced-Lighting/SSAO 를 참고하여 단순 구현 자체는 난이도가 높지는 않았다.
  - 하지만, Edge 문제, Alpha Cliped에서의 문제, Filckering 등 다양한 문제가 존재했고 해결할 수 있었던 챕터였다.
- 단순히 Blur, Filter하면 Gaussian 밖에 모르는 나에게 Edge Preserve 방식의 Blur를 학습할 수 있는 계기였다.
  - 원래는 차폐가 없는 곳이 섞이는게 싫어서 단순히 차폐가 있는 곳에 대한 Bloom을 처리했지만 불필요한 차폐가 번져 문제가 있었고 다른 Filtering 방식을 찾아봤다.
- Edge 픽셀에 대해서 SampleWeight를 두고 최적화하는 방식도 알 수 있었다.
