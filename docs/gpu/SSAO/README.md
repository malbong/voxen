# SSAO (Screen-Space Ambient Occlusion)

## 1. 개요

SSAO는 **화면 공간(Screen-Space)에서 주변 기하의 차폐(Occlusion)를 근사**하여, 구석이나 틈새에 자연스러운 그림자를 생성하는 기법이다. 전역 조명(Global Illumination) 없이도 공간감과 깊이감을 크게 향상시킨다.

Voxen에서는 Deferred Shading 파이프라인의 일부로, G-Buffer의 Position/Normal 정보를 활용하여 View Space에서 반구 샘플링 기반 SSAO를 계산한다.

## 2. 도입 동기

복셀 환경은 직각의 모서리와 평면이 반복되는 구조다. Ambient Light만 있으면 모든 면이 균일하게 밝아져, 블록 사이의 구석이나 벽과 바닥의 접합부가 시각적으로 구분되지 않는다.

SSAO를 적용하면 **기하적으로 차폐된 영역이 어두워져**, 블록 사이의 틈, 동굴 내부, 처마 밑 등에 자연스러운 접촉 그림자(Contact Shadow)가 생긴다. 이는 복셀의 직각 구조에서 특히 효과적인데, 모서리마다 뚜렷한 차폐 관계가 존재하기 때문이다.

## 3. 핵심 아이디어

### 3.1 SSAO의 원리

SSAO의 핵심 질문은: **"이 픽셀 주변에 얼마나 많은 기하가 가려(차폐)하고 있는가?"** 이다.

각 픽셀에 대해:

1. View Space에서 해당 픽셀의 **위치(P)와 법선(N)** 을 가져온다
2. 법선 방향 **반구(Hemisphere)** 내에 무작위 샘플 포인트를 배치한다
3. 각 샘플 포인트를 화면에 투영하여, **그 위치에 실제로 기록된 깊이(Depth)** 와 비교한다
4. 샘플 포인트가 실제 기하보다 **뒤에** 있으면 → 그 방향은 차폐됨(Occluded)
5. 차폐된 샘플의 비율이 Occlusion Factor가 된다

```
          N (법선)
          ↑
    ○ ○ ○ | ○ ○ ○     ← 반구 위 샘플 포인트들
   ○  ○   |   ○  ○
  ─────────P─────────  ← 표면
  ████████████████████  ← 근처 기하 (차폐물)

  → 기하에 가려진 샘플 = Occluded → 어두워짐
```

### 3.2 View Space에서 수행하는 이유

SSAO를 View Space에서 수행하는 것은 표준적인 접근이다:

- **깊이 비교의 단순화**: View Space에서 Z값은 카메라로부터의 거리이므로, "앞/뒤" 판정이 Z 비교 하나로 가능하다
- **투영 후 화면 좌표 변환이 용이**: View Space 샘플 포인트를 Projection Matrix로 곱하면 바로 텍스처 좌표를 얻는다
- **법선 기반 반구 방향**이 카메라 기준으로 일관되어 시각적으로 안정적이다

### 3.3 노이즈 회전을 통한 Banding 제거

16개의 샘플만으로 반구를 촘촘히 채우기는 부족하다. 적은 샘플 수에서는 동일 방향 패턴이 반복되어 **줄무늬(Banding)** 가 보인다.

이를 해결하기 위해 픽셀마다 **랜덤 회전 벡터**를 적용하여 샘플 커널을 회전시킨다. 같은 16개 방향이지만 픽셀마다 다른 각도로 회전되므로, Banding이 **고주파 노이즈**로 분산된다. 이 노이즈는 이후 Blur로 제거한다.

## 4. 구현 내용

### 4.1 CPU: 샘플 커널과 노이즈 생성 (PostEffect.cpp)

#### 샘플 커널 (sampleKernel[16])

```cpp
for (int i = 0; i < 16; ++i) {
    Vector4 sampleKernel;
    sampleKernel.x = randomFloats(generator) * 2.0f - 1.0f;  // [-1, 1]
    sampleKernel.y = randomFloats(generator) * 2.0f - 1.0f;  // [-1, 1]
    sampleKernel.z = randomFloats(generator);                  // [0, 1] ← 반구 (법선 방향만)
    sampleKernel.Normalize();
    sampleKernel *= randomFloats(generator);

    float scale = (float)i / 16;
    sampleKernel *= Lerp(0.1f, 1.0f, scale * scale);
}
```

- **X, Y**: `[-1, 1]` 범위 — 법선에 수직인 평면 위의 임의 방향
- **Z**: `[0, 1]` 범위 — 법선 방향(양의 반구)으로만 샘플. 음수가 없으므로 표면 아래로 샘플링하지 않는다
- **정규화 후 랜덤 스케일링**: 단위 반구 위의 점을 잡은 뒤, 랜덤 길이를 곱해 반구 **내부**에 균일하게 분포시킨다
- **`Lerp(0.1, 1.0, scale²)`**: 인덱스가 작을수록 짧은 오프셋(표면 가까이), 클수록 긴 오프셋. **가까운 샘플이 더 많아** 디테일한 차폐를 포착하면서도 먼 차폐도 감지한다

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

16개의 정규화된 랜덤 방향 벡터를 4×4 그리드로 배열한다. 이 벡터들은 셰이더에서 **Gram-Schmidt 과정**을 통해 법선 주위의 TBN 기저를 구성하는 데 사용된다.

### 4.2 GPU: 노이즈 인덱싱 (SsaoPS.hlsl)

SSAO에서 노이즈를 적용하는 가장 일반적인 방법은 **4×4 노이즈 텍스처를 타일링**하는 것이다. Voxen에서는 노이즈를 **Constant Buffer(배열)**로 전달하고, 셰이더에서 픽셀 좌표의 **정수 나머지 연산**으로 직접 인덱싱한다.

```hlsl
uint ix = uint(screenPos.x) % 4;
uint iy = uint(screenPos.y) % 4;
float3 randomVec = normalize(rotationNoise[ix + 4 * iy].xyz);
```

#### 픽셀 좌표 모듈로 인덱싱

`screenPos`는 `SV_POSITION`으로부터 얻은 픽셀 좌표다. `% 4` 연산으로 x, y 각각 `{0, 1, 2, 3}` 인덱스가 4픽셀 주기로 반복되며, `rotationNoise[ix + 4 * iy]`로 **16개 배열 전체를 균등하게 참조**한다.

화면 해상도와 비율에 무관하게 항상 4×4 픽셀 단위의 정사각형 타일이 보장된다. `appWidth`와 `appHeight`로 스케일링하지 않고 정수 모듈로를 사용하기 때문에, x/y 축 타일링 주기가 동일하게 유지된다.

#### 4×4 배열 완전 활용

이전 구현의 `frac(texcoord * appWidth / 2.0) * 3.0` + bilinear 보간 방식은, 픽셀 좌표가 정수이므로 `frac(pixel / 2.0)`이 0 또는 0.5만 가졌다. fx는 0 또는 1.5만 취하게 되어, **배열의 마지막 행/열(인덱스 3)에 접근하지 않는 낭비**가 있었다.

모듈로 인덱싱은 4픽셀 주기마다 `{0, 1, 2, 3}`을 정확히 한 번씩 순환하여 16개 노이즈 벡터 모두를 균등하게 활용한다.

### 4.3 법선 방향 반구 구성 (TBN)

```hlsl
float3 T = normalize(randomVec - viewNormal * dot(viewNormal, randomVec));
float3 B = cross(viewNormal, T);
float3x3 TBN = float3x3(T, B, viewNormal);
```

랜덤 벡터를 법선 평면에 투영하여 Tangent를 구하는 **Gram-Schmidt 직교화**다.

1. `randomVec`에서 법선 방향 성분을 제거 → 법선에 수직인 벡터 `T`
2. `N × T = B` → 세 번째 직교 축
3. `TBN = [T, B, N]` — 법선을 Z축으로 하는 직교 기저

이 TBN 행렬로 샘플 커널을 변환하면, **법선 방향의 반구**에 맞춰 샘플이 배치된다. 랜덤 벡터가 픽셀마다 다르므로, 동일한 16개 커널 방향이 픽셀마다 다른 각도로 회전된다.

### 4.4 반구 샘플링과 차폐 판정

```hlsl
float radius = 1.5;
float bias = 0.05;

for (uint i = 0; i < 16; ++i)
{
    // 1. 샘플 위치 계산 (View Space)
    float3 sampleOffset = mul(sampleKernel[i].xyz, TBN);
    float3 samplePos = viewPos + sampleOffset * radius;

    // 2. 화면 좌표로 투영
    float4 sampleProjPos = mul(float4(samplePos, 1.0), proj);
    sampleProjPos.xyz /= sampleProjPos.w;

    float2 sampleTexcoord;
    sampleTexcoord.x = saturate(sampleProjPos.x * 0.5 + 0.5);
    sampleTexcoord.y = saturate(-(sampleProjPos.y * 0.5) + 0.5);

    // 3. 해당 화면 위치의 실제 깊이 로드
    float4 position = positionTex.Load(sampleScreenCoord, 0);
    float4 storedViewPos = mul(float4(position.xyz, 1.0), view);

    // 4. 깊이 비교 → 차폐 판정
    float rangeCheck = pow(smoothstep(0.0, 1.0, radius / distance), 2.0);
    occlusionFactor += (storedViewPos.z + bias < samplePos.z ? 1.0 : 0.0) * rangeCheck;
}
```

단계별 원리:

1. **샘플 오프셋 변환**: 커널의 반구 좌표를 TBN으로 회전한 뒤, 현재 픽셀의 View Space 위치에 `radius`만큼 떨어진 점을 계산한다

2. **화면 투영**: View Space 샘플 점을 Projection Matrix로 투영하여 텍스처 좌표를 구한다. NDC `[-1,1]`을 UV `[0,1]`로 변환하며, Y축은 DX 좌표계에 맞춰 반전한다

3. **실제 기하 깊이 로드**: 투영된 텍스처 좌표 위치의 G-Buffer에서 World Position을 읽고, View Space로 변환한다. `position.w == -1.0`인 경우(하늘/빈 공간)는 매우 먼 거리(1000)로 설정하여 차폐에 기여하지 않게 한다

4. **차폐 판정**: `storedViewPos.z + bias < samplePos.z` — 실제 기하의 Z(+bias)가 샘플 점의 Z보다 작으면, **샘플 점이 기하 뒤에 묻혀 있으므로 차폐됨**

#### Range Check (거리 감쇠)

```hlsl
float w = smoothstep(0.0, 1.0, radius / max(1e-4, length(viewPos - storedViewPos.xyz)));
float rangeCheck = pow(w, 2.0);
```

**샘플링 지점의 기하가 현재 픽셀과 너무 멀리 떨어져 있으면 차폐로 인정하지 않는다.** 예를 들어, 절벽 가장자리에서 바닥이 차폐물로 감지되면 원치 않는 어둠이 생긴다.

- `radius / distance` — 기하가 반경 내에 있으면 1에 가깝고, 멀면 0에 가까움
- `smoothstep` + `pow(2)` — 거리에 따라 부드럽게 감쇠하되, 경계에서 급격히 떨어지도록

### 4.5 거리 감쇠와 최종 출력

```hlsl
float distance = length(viewPos.xyz);
float attenuation = saturate((lodRenderDistance - distance) / (lodRenderDistance - 32.0));

return 1.0 - (occlusionFactor * attenuation);
```

- `attenuation` — 카메라에서 **LOD 렌더 거리** 이상 떨어진 픽셀에서는 SSAO 효과를 점진적으로 페이드아웃한다. 먼 거리의 SSAO는 해상도 한계로 부정확해지므로, 이를 방지하는 것이다
- 최종 출력은 `1.0 - occlusion` — 차폐가 없으면 1.0(밝음), 완전 차폐면 0.0(어두움)
- 이 값은 이후 `ShadingBasicPS.hlsl`에서 `ao = pow(ssao, 4.0)`으로 강화되어 Ambient Lighting에 곱해진다

### 4.6 MSAA Edge 처리 (mainMSAA)

Edge 픽셀에서는 4개 MSAA 샘플 각각에 대해 SSAO를 계산한다. 단, **Alpha-Clip 기하 포함 여부에 따라 두 경로**로 분기하여, 가능한 경우 Coverage 기반 가중치로 중복 연산을 줄인다.

#### semiAlphaCount 검사

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

G-Buffer 채우기 패스에서 Alpha-Clip 기하가 있는 샘플은 `normalEdgeTex.w = 2.0`으로 마킹된다. 이 값을 읽어 픽셀 내 Alpha-Clip 샘플 수를 파악한다.

#### coverageAnalysis — SV_Coverage 기반 중복 샘플 병합

```hlsl
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

`coverageTex`에는 G-Buffer 채우기 패스의 `SV_Coverage` 값이 저장되어 있다. Coverage 값이 동일한 두 샘플은 **동일한 기하를 덮고 있음**을 의미하므로, 대표 샘플의 weight를 증가시키고 나머지를 0으로 처리하여 skip한다.

예를 들어 4샘플 중 3개가 동일 표면을 덮는 경우:

```
sample 0: weight 3  ← 대표, getOcclusionFactor 1회 계산
sample 1: weight 0  ← skip
sample 2: weight 0  ← skip
sample 3: weight 1  ← 다른 표면, getOcclusionFactor 1회 계산
→ getOcclusionFactor 2회 (vs 미사용 시 4회)
```

#### 분기 처리

```hlsl
if (semiAlphaCount == 0)
{
    // SV_Coverage 기반 weight 계산 → 중복 샘플 skip
    sampleWeight = coverageAnalysis(coverage);
    sampleWeightArray[0..3] = sampleWeight.xyzw;
}
// semiAlphaCount > 0: 모든 weight = 1 유지 (전체 순회)
```

- **semiAlphaCount == 0**: Alpha-Clip 기하 없음 → Coverage 값이 표면 동일성의 신뢰할 수 있는 기준이 되므로 `coverageAnalysis` 적용
- **semiAlphaCount > 0**: Alpha-Clip 기하 존재 → Coverage 마스크가 클립 여부를 반영하여 표면 동일성 판단에 사용할 수 없으므로 모든 샘플을 독립적으로 계산

#### 실측 결과

SSAO 패스 단독 기준 **110μs → 100μs** (약 9% 개선). 단, `mainMSAA`는 전체 화면 중 Edge 픽셀에서만 실행되므로 전체 프레임 렌더링 시간에 대한 기여는 미미하다.

### 4.7 Edge-Preserving Blur (BlurBilateralPS.hlsl)

SSAO 결과의 노이즈를 제거하되, 차폐 경계를 보존하기 위해 **Bilateral Filter** 를 사용한다.

#### 핵심 설계

```hlsl
float4 base = renderTex.Sample(linearClampSS, input.texcoord);
float sigma = 0.325;

// center 픽셀은 항상 포함 — sumWeight 최소 1.0 보장
sumColor  += base;
sumWeight += 1.0;

for (int i = -1; i <= 1; ++i)
for (int j = -1; j <= 1; ++j)
{
    if (i == 0 && j == 0) continue;

    float4 s    = renderTex.Sample(linearClampSS, input.texcoord + offset);
    float  diff = length(base - s);

    // range weight: 값이 유사할수록 높은 기여
    // occlusion weight: 차폐가 높은 샘플일수록 높은 기여
    float w = exp(-diff * diff / (sigma * sigma)) * pow(s, 1.25);

    sumColor  += s * w;
    sumWeight += w;
}
return sumColor / sumWeight;
```

두 가지 weight가 결합된다:

- **Range weight** `exp(-diff²/σ²)`: SSAO 값 차이가 클수록(=경계) weight를 0으로 수렴시켜 섞지 않는다. σ가 작을수록 경계 차단이 예민해진다

- **Occlusion weight** `pow(s, 1.25)`: Range weight만으로는 해결되지 않는 문제를 보완한다. Alpha-Clip 잎사귀 경계처럼 차폐가 있는 픽셀(s≈1.0)과 차폐가 없는 빈 픽셀(s≈0.0)이 인접할 때, Range weight가 충분히 차단하더라도 낮은 차폐 샘플이 조금씩 섞이면 occluded 영역의 차폐값이 희석된다. Occlusion weight는 샘플 자체의 차폐 정도를 가중치로 부여하여, 차폐가 낮은 샘플(빈 공간, Alpha-Clip 구멍)의 영향력을 원천적으로 억제한다. `pow(s, 1.25)`의 지수는 이 억제를 선형보다 강하게 적용하기 위한 튜닝 값이다

결과적으로 두 weight의 역할이 명확히 분리된다: **Range weight가 경계를 감지하고, Occlusion weight가 경계 너머 빈 공간의 영향력을 제거**한다.

#### 2D 단일 패스 (비분리)

Gaussian Blur는 `G(x,y) = G(x) × G(y)` 로 분리되어 X→Y 2패스로 구현 가능하다. Bilateral은 range weight가 2D 이웃 전체의 값 차이에 의존하므로 **수학적으로 분리 불가능**하다.

X 패스 후 경계 픽셀의 값이 중간값으로 변하면, Y 패스에서 그 중간값을 center로 사용해 range weight를 계산하게 되어 경계 보존이 무너진다. 따라서 3×3 이웃을 한 번에 처리하는 **단일 2D 패스**로 구현한다.

#### Ping-pong 버퍼 구성

단일 패스라도 동일 버퍼 읽기/쓰기 충돌을 막기 위해 ping-pong 방식을 사용한다:

```
Pass 1: ssaoSRV        → ssaoBlurRTV[0]
Pass 2: ssaoBlurSRV[0] → ssaoBlurRTV[1]
Pass 3: ssaoBlurSRV[1] → ssaoBlurRTV[0]
        ...
마지막:                → CopyResource → ssaoBuffer
```

### 4.8 전체 파이프라인

```
[CPU - PostEffect.cpp]
sampleKernel[16]: 반구 내 랜덤 방향, 가까울수록 밀집
rotationNoise[16]: 4×4 랜덤 회전 벡터
        ↓ Constant Buffer

[GPU - SsaoPS.hlsl::getOcclusionFactor()]
screenPos → % 4 → 4×4 노이즈 인덱스
randomVec → Gram-Schmidt → TBN (법선 방향 반구)
        ↓
16 samples × (TBN 변환 → View Space offset → 화면 투영 → 깊이 비교)
        ↓
occlusionFactor / 16 → 거리 감쇠 → 차폐값 저장
        ↓ BlurBilateralPS (3×3, range weight + occlusion weight)
ssaoTex → ShadingBasicPS에서 pow(ao, 4) 후 Ambient에 곱함
```

## 5. 문제점 & 해결

### 5.1 View 변경 시 노이즈 Flickering

**문제**: 노이즈 인덱스 계산에 부동소수점 연산이 개입하면, 카메라의 미세한 이동 시 동일한 픽셀에 할당되는 노이즈 벡터가 달라져 **깜빡거림(Flickering)** 이 발생할 수 있다.

**해결**: `uint(screenPos.x) % 4`로 정수 픽셀 좌표에 직접 나머지 연산을 적용한다. 화면의 픽셀 (x, y)는 항상 동일한 나머지 인덱스 `(x%4, y%4)`로 매핑되므로, 카메라가 어떻게 이동하든 동일 픽셀은 항상 동일한 노이즈 벡터를 사용한다. 부동소수점 정밀도 문제가 개입할 여지가 없어 Flickering이 구조적으로 제거된다.

### 5.2 적은 샘플 수에 의한 Banding

**문제**: 16개 샘플로는 반구 전체를 균일하게 커버할 수 없어, 동일 패턴이 반복되는 줄무늬(Banding)가 보인다.

**해결**: 픽셀마다 다른 회전 벡터로 TBN 기저를 회전시켜, Banding을 고주파 노이즈로 분산시킨다. 이 노이즈는 이후 Blur 패스(2회)로 평활화된다. 결과적으로 16개 샘플만으로도 시각적으로 충분히 부드러운 AO를 얻는다.

### 5.3 Alpha-Clip 경계에서의 Blur Artifact

**문제**: 잎사귀처럼 Alpha Clip된 메시의 경계에서, 실제 차폐가 있는 픽셀(SSAO≈1.0)과 Alpha Clip으로 비어있는 픽셀(SSAO≈0.0)이 인접한다. 일반 Gaussian Blur는 이 둘을 평균내어 경계에 0.5 중간값을 생성한다. 결과적으로 잎사귀 주변에 흐릿한 테두리(Half-Occlusion Ring)가 나타난다.

초기에는 **Bloom 기반 스무딩**으로 해결했다. Bloom의 down/up sampling은 단순 평균이 아니라 밝은(occluded) 값을 주변으로 확장하는 경향이 있어, 0.5 중간값 생성 없이 차폐 영역을 자연스럽게 퍼뜨린다. 그러나 SSAO 노이즈가 그대로 증폭되어 시각적으로 두드러지는 문제가 있었다.

**최종 해결**: Range weight와 Occlusion weight를 결합한 Bilateral Filter. 0.0↔1.0 경계에서 range weight가 0에 수렴하여 섞지 않으며, occlusion weight가 낮은 차폐 샘플(빈 공간)의 영향력을 억제한다.

### 5.4 Alpha-Clip 경계에서의 SV_Coverage 신뢰성 문제

**문제**: Coverage 값이 동일한 두 샘플을 "동일 기하"로 판단하는 `coverageAnalysis`는, Alpha-Clip 기하가 있는 픽셀에서 신뢰할 수 없다. Alpha-Clip은 서브픽셀 단위로 클립을 적용하므로 동일한 Coverage 마스크를 가지더라도 실제로는 서로 다른 표면(클립된 잎사귀 / 뒤의 배경)일 수 있다. 이 경우 `coverageAnalysis`가 다른 표면을 동일 기하로 묶어 weight를 부여하면 잘못된 occlusionFactor가 계산된다.

**해결**: G-Buffer 채우기 패스에서 Alpha-Clip 기하 샘플에 `normalEdgeTex.w = 2.0`으로 마킹한다. `mainMSAA`에서 `semiAlphaCount`를 검사하여, Alpha-Clip 샘플이 하나라도 있으면 `coverageAnalysis`를 건너뛰고 모든 샘플을 weight = 1로 독립 계산한다.

### 5.6 Bilateral Filter의 비분리성

**문제**: Gaussian Blur처럼 X→Y 2패스 분리를 시도했다. X 패스 이후 경계 픽셀에 중간값이 생기고, Y 패스에서 그 중간값을 center로 삼아 range weight를 계산하면 경계 양쪽 샘플을 동등하게 취급하여 경계 보존이 무너진다.

**해결**: 3×3 이웃을 한 번에 처리하는 2D 단일 패스로 구현한다. 다만 **Multi-pass 2D bilateral**(완전한 2D 연산을 여러 번 반복)은 각 패스가 수학적으로 올바르기 때문에 유효하다.

### 5.7 먼 거리 기하에 의한 오차 (Halo Artifact)

**문제**: 반경 내에 실제로는 관련 없는 먼 거리 기하가 감지되면, 가장자리에 비정상적인 어두운 테두리(Halo)가 생긴다.

**해결**: `rangeCheck = pow(smoothstep(0, 1, radius / distance), 2)` — 샘플 반경보다 먼 기하의 차폐 기여를 급격히 감쇠시킨다. 또한 `position.w == -1.0`(빈 공간)일 때 Z를 1000으로 설정하여, 하늘이 차폐물로 오인되지 않도록 한다.

## 6. 결과

- 블록 모서리, 벽과 바닥 접합부, 동굴 내부 등에 **자연스러운 접촉 그림자**가 생겨 공간감이 크게 향상된다
- 16 샘플 + 4×4 노이즈 회전 + Bilateral Blur 조합으로, 적은 연산량에 비해 시각적으로 충분한 품질을 달성한다
- 픽셀 좌표 모듈로 인덱싱으로, 카메라 이동/회전 시에도 **Flickering 없이 안정적인 AO** 를 유지한다
- 거리 감쇠(`attenuation`)로 먼 거리에서의 부정확한 AO가 자연스럽게 사라진다
- Range weight + Occlusion weight 결합 Bilateral Filter로 Alpha-Clip 잎사귀 경계에서도 **경계 artifact 없이 부드러운 AO** 를 달성한다

## 7. 회고

- 현재 샘플 반경(`radius = 1.5`)과 편향(`bias = 0.05`)이 하드코딩되어 있다. 이를 파라미터화하면 씬에 따라 AO 범위와 강도를 조절할 수 있을 것이다
- Bilateral Filter는 수학적으로 분리 불가능(non-separable)하여 X/Y 2패스로 구현할 수 없고, 3×3 단일 패스로 처리한다. 더 넓은 커널이 필요하다면 Multi-pass 2D bilateral을 반복하는 방식이 유일한 선택이다
- 현재는 단일 반경으로 샘플링하지만, 멀티 스케일 AO(여러 반경으로 합산)를 도입하면 근거리 접촉 그림자와 원거리 대규모 차폐를 동시에 표현할 수 있다
- SSAO 결과에 `pow(ao, 4.0)`을 적용하는 것은 시각적 튜닝이지만, HDR 파이프라인에서 톤 매핑과의 상호작용을 고려하면 감마 커브보다는 물리 기반의 가중치가 더 적절할 수 있다
- Geometry 기반 Bilateral(G-Buffer position/normal을 경계 판단 기준으로 사용)을 도입하면 SSAO 값 노이즈에 독립적인 더 안정적인 경계 보존이 가능하다
