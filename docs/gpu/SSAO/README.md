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

### 4.2 GPU: 노이즈 텍스처 좌표와 Flickering 방지 (SsaoPS.hlsl)

SSAO에서 노이즈를 적용하는 가장 일반적인 방법은 **4×4 노이즈 텍스처를 타일링**하는 것이다. 보통 이를 위해 별도의 Texture2D를 만들고 Wrap Sampler로 반복시킨다. 그러나 Voxen에서는 노이즈를 **Constant Buffer(배열)**로 전달하고, 셰이더에서 **수동 Bilinear 보간**을 수행한다.

```hlsl
float fx = frac(texcoord.x * appWidth / 2.0) * 3.0;
float fy = frac(texcoord.y * appHeight / 2.0) * 3.0;
```

이 좌표 계산이 이 구현에서 가장 주의 깊게 설계된 부분이다. 단계별로 분석하면:

#### (1) `texcoord.x * appWidth` — 텍스처 좌표를 픽셀 좌표로 변환

`texcoord`는 `[0, 1]` 범위의 정규화된 화면 좌표이므로, `appWidth`를 곱하면 실제 픽셀 위치가 된다.

#### (2) `/ 2.0` — 2×2 픽셀 블록 단위로 그룹화

`/ 2.0`으로 나누면 **인접한 2×2 픽셀이 같은 정수 구간**에 속하게 된다. 즉, 2×2 픽셀 블록이 동일한 회전 벡터를 공유한다.

왜 2×2인가? SSAO 결과는 이후 Blur로 평활화되는데, 너무 고주파(1px 단위)의 노이즈는 Blur로도 깨끗하게 지워지지 않고, 너무 저주파(4px 단위)의 노이즈는 Banding으로 보인다. **2×2는 Blur 커널과 노이즈 주파수의 균형점**이다.

#### (3) `frac(...)` — 정수 부분 제거, [0, 1) 반복

`frac()`은 소수 부분만 취하여 값을 `[0, 1)` 범위로 반복시킨다.

**여기서 핵심: 왜 `frac(texcoord.x * appWidth / 2.0)`이고 `frac(pixelCoord / 2.0)`이 아닌가?**

만약 `SV_POSITION`(픽셀 좌표)에 `frac()`을 적용하면, 카메라가 회전하거나 이동할 때 **화면 위의 고정 위치에 동일한 노이즈 패턴이 묶여 있게 된다**. 즉, 화면의 (100, 100) 픽셀은 카메라가 어디를 보든 항상 같은 회전 벡터를 사용한다.

이것 자체는 SSAO에서 일반적으로 문제가 되지 않는 경우도 있지만, **카메라가 서브 픽셀 단위로 미세하게 움직일 때** 문제가 된다. 화면 좌표(`SV_POSITION`)는 항상 정수+0.5이므로 카메라의 미세 이동을 반영하지 못한다. 반면 `texcoord`는 풀스크린 쿼드의 보간된 UV이므로, 카메라 움직임에 따라 **연속적으로 변한다**.

`texcoord × appWidth`는 픽셀 단위와 동일한 스케일이지만, `texcoord` 자체가 보간된 값이므로 카메라 움직임에 안정적으로 연동된다. 이로 인해 **카메라가 미세하게 회전/이동할 때 노이즈 패턴이 갑자기 바뀌는 Flickering이 방지**된다.

#### (4) `* 3.0` — [0, 1)을 [0, 3)으로 확장 → 4×4 그리드 인덱싱

4×4 = 16개의 노이즈 벡터가 있으므로, 인덱스 범위는 `[0, 3]`이다. `[0, 1)` 범위를 `* 3.0`으로 확장하면 `[0, 3)` 범위가 되어, `floor()`로 정수 인덱스 `{0, 1, 2, 3}`을 얻을 수 있다.

#### 수동 Bilinear 보간

```hlsl
uint fx1 = uint(floor(fx));       // 좌하단 인덱스
uint fx2 = uint(floor(fx + 1.0)); // 우상단 인덱스
uint fy1 = uint(floor(fy));
uint fy2 = uint(floor(fy + 1.0));

float3 v1 = lerp(rotationNoise[fx1 + 4*fy1].xyz, rotationNoise[fx2 + 4*fy1].xyz, frac(fx));
float3 v2 = lerp(rotationNoise[fx1 + 4*fy2].xyz, rotationNoise[fx2 + 4*fy2].xyz, frac(fx));
float3 randomVec = normalize(lerp(v1, v2, frac(fy)));
```

Constant Buffer 배열은 하드웨어 Sampler를 사용할 수 없으므로, 4개의 이웃 노이즈 벡터를 `lerp`로 수동 보간한다. 이는 Texture2D + Linear Wrap Sampler와 동일한 결과를 내지만, 별도 텍스처 없이 상수 버퍼만으로 구현한 것이다.

2×2 픽셀 블록의 경계에서 `frac(fx)`, `frac(fy)`가 0~1 사이를 부드럽게 변하므로, **인접 블록 간 노이즈 벡터가 선형 보간**되어 패턴 전환이 부드럽다.

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
float radius = 0.75;
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

Edge 픽셀에서는 4개 MSAA 샘플 각각에 대해 개별적으로 SSAO를 계산한다. `coverageAnalysis()`로 중복 Coverage를 분석하여, 실제로 서로 다른 기하를 덮는 샘플만 개별 처리하고, 같은 기하를 덮는 샘플은 가중치로 합산하여 불필요한 연산을 줄인다.

### 4.7 전체 파이프라인

```
[CPU - PostEffect.cpp]
sampleKernel[16]: 반구 내 랜덤 방향, 가까울수록 밀집
rotationNoise[16]: 4×4 랜덤 회전 벡터
        ↓ Constant Buffer

[GPU - SsaoPS.hlsl::getOcclusionFactor()]
texcoord → frac(texcoord * appSize / 2) * 3 → 4×4 노이즈 인덱스
        ↓ Bilinear 보간
randomVec → Gram-Schmidt → TBN (법선 방향 반구)
        ↓
16 samples × (TBN 변환 → View Space offset → 화면 투영 → 깊이 비교)
        ↓
occlusionFactor / 16 → 거리 감쇠 → (1 - occlusion)
        ↓ Blur
ssaoTex → ShadingBasicPS에서 pow(ao, 4) 후 Ambient에 곱함
```

## 5. 문제점 & 해결

### 5.1 View 변경 시 노이즈 Flickering

**문제**: 픽셀 좌표(`SV_POSITION`)를 기반으로 노이즈 인덱스를 계산하면, 카메라가 미세하게 움직일 때 동일 오브젝트 위의 노이즈 패턴이 갑자기 바뀌어 **깜빡거림(Flickering)** 이 발생한다. `SV_POSITION`은 항상 정수+0.5 값이므로 서브 픽셀 움직임을 반영하지 못하기 때문이다.

**해결**: `frac(texcoord.x * appWidth / 2.0) * 3.0`으로, 풀스크린 쿼드의 **보간된 UV(`texcoord`)** 를 기반으로 계산한다. `texcoord`는 카메라 변화에 연속적으로 대응하므로, 노이즈 패턴이 화면에 고정되지 않고 부드럽게 변화한다. 이로써 카메라 회전/이동 시 Flickering이 제거된다.

### 5.2 적은 샘플 수에 의한 Banding

**문제**: 16개 샘플로는 반구 전체를 균일하게 커버할 수 없어, 동일 패턴이 반복되는 줄무늬(Banding)가 보인다.

**해결**: 픽셀마다 다른 회전 벡터로 TBN 기저를 회전시켜, Banding을 고주파 노이즈로 분산시킨다. 이 노이즈는 이후 Blur 패스(2회)로 평활화된다. 결과적으로 16개 샘플만으로도 시각적으로 충분히 부드러운 AO를 얻는다.

### 5.3 먼 거리 기하에 의한 오차 (Halo Artifact)

**문제**: 반경 내에 실제로는 관련 없는 먼 거리 기하가 감지되면, 가장자리에 비정상적인 어두운 테두리(Halo)가 생긴다.

**해결**: `rangeCheck = pow(smoothstep(0, 1, radius / distance), 2)` — 샘플 반경보다 먼 기하의 차폐 기여를 급격히 감쇠시킨다. 또한 `position.w == -1.0`(빈 공간)일 때 Z를 1000으로 설정하여, 하늘이 차폐물로 오인되지 않도록 한다.

## 6. 결과

- 블록 모서리, 벽과 바닥 접합부, 동굴 내부 등에 **자연스러운 접촉 그림자**가 생겨 공간감이 크게 향상된다
- 16 샘플 + 4×4 노이즈 회전 + Blur 조합으로, 적은 연산량에 비해 시각적으로 충분한 품질을 달성한다
- `texcoord` 기반 노이즈 좌표 계산으로, 카메라 이동/회전 시에도 **Flickering 없이 안정적인 AO** 를 유지한다
- 거리 감쇠(`attenuation`)로 먼 거리에서의 부정확한 AO가 자연스럽게 사라진다

## 7. 회고

- 현재 샘플 반경(`radius = 0.75`)과 편향(`bias = 0.05`)이 하드코딩되어 있다. 이를 파라미터화하면 씬에 따라 AO 범위와 강도를 조절할 수 있을 것이다
- Blur 패스가 2회 수행되는데, 분리 가능 필터(Separable Filter)를 사용하면 같은 커널 크기에서 더 효율적일 수 있다
- 현재는 단일 반경으로 샘플링하지만, 멀티 스케일 AO(여러 반경으로 합산)를 도입하면 근거리 접촉 그림자와 원거리 대규모 차폐를 동시에 표현할 수 있다
- SSAO 결과에 `pow(ao, 4.0)`을 적용하는 것은 시각적 튜닝이지만, HDR 파이프라인에서 톤 매핑과의 상호작용을 고려하면 감마 커브보다는 물리 기반의 가중치가 더 적절할 수 있다
