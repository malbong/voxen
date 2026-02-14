## 1. 개요

- **Cascade Shadow Map(CSM)**은 넓은 오픈월드에서 먼 거리의 그림자까지 효율적으로 렌더링하는 기법
- 카메라 프러스텀을 **3개 구간(Cascade)**으로 분할하여, 각 구간마다 독립적인 쉐도우맵 생성
- 가까운 구간은 높은 해상도, 먼 구간은 낮은 해상도로 그림자 품질과 성능의 균형
- 프로젝트에서 **실시간 동적 그림자 시스템**의 핵심 기반 기술

## 2. 도입 동기

- **오픈월드의 넓은 시야 거리**: 기본 쉐도우맵으로는 먼 거리까지 커버하면 texel 밀도가 낮아져 계단 현상(aliasing) 발생
- **가까운 곳의 고품질 그림자 필요**: 플레이어 주변은 높은 해상도로 섬세한 그림자 표현
- **먼 거리의 그림자 커버**: 먼 거리도 그림자가 보이지만 낮은 품질 허용
- **성능 최적화**: 단일 고해상도 쉐도우맵(예: 4096x4096)은 메모리/성능 비용 과다

## 3. 핵심 아이디어

### 3.1 Cascade 분할

```
Cascade 0: [0.0,   0.015] → 카메라로부터 1.5% 범위 (가장 가까움, 고해상도)
Cascade 1: [0.015, 0.035] → 카메라로부터 1.5%~3.5% 범위 (중간 해상도)
Cascade 2: [0.035, 0.15 ] → 카메라로부터 3.5%~15% 범위 (먼 거리, 저해상도)
```

- 카메라 프러스텀(NDC: -1~1 큐브)을 Near-Far 방향으로 4개 평면으로 분할
- 각 Cascade는 분할된 프러스텀 부분만 커버하는 Orthographic Light Projection 사용
- 가까운 Cascade일수록 좁은 월드 범위를 커버 → 같은 1024x1024 텍스처로 높은 texel 밀도

### 3.2 Texel Snapping (텍셀 스냅)

```cpp
float worldUnitsPerTexel = cascadeBound / CASCADE_SIZE; // 텍셀 하나당 월드 단위
lightViewCornerMin /= worldUnitsPerTexel;
lightViewCornerMin = floor(lightViewCornerMin); // 텍셀 경계에 정렬
lightViewCornerMin *= worldUnitsPerTexel;
```

- 카메라 이동 시 쉐도우맵이 텍셀 단위로 고정되어 "떨림(shimmer)" 방지
- Light View 공간에서 AABB를 texel 단위로 스냅

### 3.3 PCF (Percentage Closer Filtering) with Bias

```hlsl
// 3x3 PCF 커널
for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
        percentLit += shadowTex.SampleCmpLevelZero(..., lightProj.z - bias).r;
    }
}
shadowValue = percentLit / 10.0; // 중앙 1회 + 주변 9회 = 총 10회 샘플링
```

- **PCF**: 그림자 경계를 부드럽게 만드는 필터링
- **Bias**: Normal 기반 동적 bias로 Shadow Acne 방지
  ```hlsl
  bias = 0.001 + 0.01 * pow(1.0 - max(dot(lightDir, normal), 0.0), 3.0)
  ```

## 4. 구현 내용

### 4.1 CPU 측 (Light.cpp)

**Cascade 설정**

```cpp
float cascade[4] = { 0.0f, 0.015f, 0.035f, 0.15f }; // 4개 평면으로 3개 cascade 정의
float diagonals[3] = { 30.0f, 88.0f, 337.0f };     // 각 cascade의 대각선 길이
```

**Cascade 별 ViewProj 계산 과정**

1. **카메라 프러스텀 코너 계산**

   ```cpp
   Vector3 frustumCorner[8]; // NDC 공간 (-1~1)
   // camera ViewProj Inverse로 월드 공간 변환
   worldFrustumCorner[i] = Transform(frustumCorner[i], cameraViewProjInverse);
   ```

2. **Cascade별 슬라이싱**

   ```cpp
   for (각 Cascade i) {
       // Near-Far 보간으로 cascade 경계 계산
       worldCascadeCorner[j] = worldFrustumCorner[j] + worldBeginToEnd * cascade[i];
       // Light View 공간으로 변환
       lightViewCascadeCorner[j] = Transform(worldCascadeCorner[j], lightViewMatrix);
   }
   ```

3. **Light View 공간에서 AABB 계산**

   ```cpp
   for (8개 코너) {
       lightViewCornerMin = Min(lightViewCornerMin, lightViewCascadeCorner[j]);
       lightViewCornerMax = Max(lightViewCornerMax, lightViewCascadeCorner[j]);
   }
   ```

4. **Border Offset 추가 (여유 공간)**

   ```cpp
   Vector3 maxminVector = lightViewCornerMax - lightViewCornerMin;
   Vector3 borderOffset = (diagonal - maxminVector) * 0.5f;
   lightViewCornerMax += borderOffset;
   lightViewCornerMin -= borderOffset;
   ```

5. **Texel Snapping**

   ```cpp
   float worldUnitsPerTexel = cascadeBound / CASCADE_SIZE;
   lightViewCornerMin /= worldUnitsPerTexel;
   lightViewCornerMin = floor(lightViewCornerMin); // 텍셀 경계에 정렬
   lightViewCornerMin *= worldUnitsPerTexel;
   ```

6. **Orthographic Projection 생성**
   ```cpp
   m_proj[i] = XMMatrixOrthographicOffCenterLH(
       lightViewCornerMin.x, lightViewCornerMax.x,
       lightViewCornerMin.y, lightViewCornerMax.y,
       lightViewCornerMin.z, lightViewCornerMax.z
   );
   ```

### 4.2 GPU 측 (Common.hlsli)

**getShadowFactor() 함수**

```hlsl
float getShadowFactor(float3 posWorld, float3 normal)
{
    float pcfMargin = 0.02; // Cascade 경계 마진

    [loop]
    for (int i = 0; i < 3; ++i) // 3개 cascade 순회
    {
        // 1. Light Projection 공간으로 변환
        float4 lightProj = mul(float4(posWorld, 1.0), shadowViewProj[i]);
        lightProj.xyz /= lightProj.w;

        // 2. Cascade 범위 체크 (margin 포함)
        if (lightProj.x < -1.0 + pcfMargin || lightProj.x > 1.0 - pcfMargin ||
            lightProj.y < -1.0 + pcfMargin || lightProj.y > 1.0 - pcfMargin ||
            lightProj.z < 0.0 + pcfMargin  || lightProj.z > 1.0 - pcfMargin)
        {
            continue; // 이 cascade 범위를 벗어나면 다음 cascade 시도
        }

        // 3. Normal 기반 동적 bias
        float bias = 0.001 + 0.01 * pow(1.0 - max(dot(lightDir, normal), 0.0), 3.0);

        // 4. Light Projection [-1, 1] → Texture [0, 1]
        float2 lightTexcoord = float2(
            lightProj.x * 0.5 + 0.5,
            lightProj.y * -0.5 + 0.5
        );

        // 5. Cascade별 텍스처 오프셋 적용
        // 3개 cascade가 하나의 3072x1024 텍스처에 수평 배치
        float2 scaledTexcoord;
        scaledTexcoord.x = (lightTexcoord.x * (viewPortWidth[i] / width)) + (topLXOffsets[i] / width);
        scaledTexcoord.y = (lightTexcoord.y * (viewPortWidth[i] / height));

        // 6. 3x3 PCF
        float percentLit = 0.0;
        percentLit = shadowTex.SampleCmpLevelZero(shadowCompareSS, scaledTexcoord, lightProj.z - bias).r;

        float delta = 0.25 / viewPortWidth[i]; // PCF 커널 크기
        [unroll]
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                percentLit += shadowTex.SampleCmpLevelZero(shadowCompareSS,
                    scaledTexcoord.xy + float2(x * delta, y * delta), lightProj.z - bias).r;
            }
        }

        // 7. 최종 그림자 값 (밤에는 그림자 약화)
        float shadowValue = percentLit / 10.0;
        return shadowValue + (1.0 - shadowValue) * (1.0 - saturate(radianceWeight / maxRadianceWeight));
    }

    return 1.0; // 모든 cascade 범위를 벗어나면 그림자 없음
}
```

### 4.3 쉐도우맵 레이아웃

**텍스처 구조**: 3072x1024 단일 텍스처

```
┌──────────┬──────────┬──────────┐
│Cascade 0 │Cascade 1 │Cascade 2 │
│ 1024x1024│ 1024x1024│ 1024x1024│
│ (Near)   │ (Middle) │  (Far)   │
└──────────┴──────────┴──────────┘
```

- `topLX[i]`: 각 cascade의 좌측 시작 X 좌표 (0, 1024, 2048)
- `viewPortWidth[i]`: 각 cascade의 너비 (모두 1024)

### 4.4 상수 버퍼

**ShadowConstantBuffer**

```cpp
struct ShadowConstantData {
    Matrix viewProj[3];        // 각 cascade의 ViewProj 행렬
    float4 topLX;              // 각 cascade의 텍스처 X 오프셋
    float4 viewPortW;          // 각 cascade의 뷰포트 너비
};
```

## 5. 문제점 & 해결

### 5.1 Cascade 경계에서 그림자 끊김

**문제**: Cascade 간 전환 시 그림자 품질 차이로 경계선이 보임

**해결**:

- `pcfMargin = 0.02`: Cascade 범위를 약간 줄여서 경계 부근에서는 다음 cascade로 넘어가도록 함
- 경계 근처에서는 더 넓은 범위를 커버하는 다음 cascade가 자연스럽게 이어받음

### 5.2 Shadow Acne (그림자 여드름)

**문제**: 깊이 정밀도 한계로 인해 표면에 얼룩덜룩한 패턴 발생

**해결**:

- **동적 Bias**:
  ```hlsl
  bias = 0.001 + 0.01 * pow(1.0 - max(dot(lightDir, normal), 0.0), 3.0)
  ```
- 법선과 광원 방향이 수직에 가까울수록(grazing angle) bias 증가
- 최소 0.001, 최대 0.011 범위

### 5.3 Shadow Shimmer (그림자 떨림)

**문제**: 카메라 이동 시 쉐도우맵 텍셀 위치가 미세하게 변하면서 떨림 발생

**해결**:

- **Texel Snapping**: Light View 공간에서 AABB를 텍셀 단위로 정렬
  ```cpp
  lightViewCornerMin = floor(lightViewCornerMin / worldUnitsPerTexel) * worldUnitsPerTexel;
  ```
- 카메라가 이동해도 쉐도우맵의 텍셀 배치가 일정하게 유지

### 5.4 Peter Panning (공중 부양)

**문제**: Bias가 너무 크면 그림자가 물체로부터 분리되어 보임

**해결**:

- Bias 범위를 적절히 제한 (0.001~0.011)
- Normal 기반 동적 조정으로 필요한 곳만 bias 증가
- PCF로 그림자 경계를 부드럽게 만들어 Peter Panning 덜 눈에 띄게 함

## 6. 결과

### 6.1 Cascade 범위별 해상도

| Cascade | 프러스텀 범위 | 대각선 길이 | 텍스처 해상도 | Texel 밀도 (world units/texel) |
| ------- | ------------- | ----------- | ------------- | ------------------------------ |
| 0       | 0.0 ~ 1.5%    | 30.0        | 1024x1024     | 0.029 (가장 높음)              |
| 1       | 1.5% ~ 3.5%   | 88.0        | 1024x1024     | 0.086 (중간)                   |
| 2       | 3.5% ~ 15%    | 337.0       | 1024x1024     | 0.329 (낮음)                   |

- Cascade 0은 플레이어 주변 약 1~2 청크 범위를 고해상도로 커버
- Cascade 2는 최대 렌더 거리의 15%까지 커버 (약 10~15 청크)

### 6.2 시각적 결과

- **가까운 거리**: 블록 모서리, 풀, 나무 잎사귀까지 섬세한 그림자
- **중간 거리**: 자연스러운 그림자 유지, cascade 전환 눈에 띄지 않음
- **먼 거리**: 부드러운 그림자, 디테일 감소하지만 전체적인 입체감 유지

### 6.3 성능

- **쉐도우맵 해상도**: 3072x1024 (단일 텍스처)
  - 기존 단일 4096x4096 대비 메모리 사용량 약 1/5 수준
- **렌더 패스**: Shadow Map Pass에서 3번 렌더링 (각 cascade당 1회)
  - Geometry Shader로 한 번에 3개 cascade에 렌더링
- **PCF 비용**: 픽셀당 10회 쉐도우맵 샘플링 (중앙 1회 + 3x3 커널 9회)

### 6.4 밤 시간 처리

```hlsl
return shadowValue + (1.0 - shadowValue) * (1.0 - saturate(radianceWeight / maxRadianceWeight));
```

- `radianceWeight`가 0에 가까우면(밤) 그림자 강도 약화
- 완전한 밤에는 그림자가 거의 보이지 않음 (현실감)

## 7. 회고

### 7.1 아쉬운 점

- **Cascade 개수 고정**: 3개로 하드코딩, 성능/품질 트레이드오프 조정 어려움
  - 더 먼 거리까지 커버하려면 Cascade 4개 이상 필요
- **Cascade 분할 비율 고정**: `[0.0, 0.015, 0.035, 0.15]`가 모든 상황에 최적이 아님
  - 실내/동굴에서는 다른 분할 비율이 더 효율적일 수 있음
- **단일 Light 한정**: 현재는 Directional Light 1개만 지원
  - Point Light, Spot Light 그림자는 미구현
- **Cascade 전환 최적화 부재**: 경계 블렌딩 없이 pcfMargin으로만 처리
  - PSSM(Parallel-Split Shadow Maps) 블렌딩 적용 시 더 부드러운 전환 가능

### 7.2 다음에 개선하고 싶은 방향

- **동적 Cascade 분할**: 카메라 상황(실내/야외, 고도 등)에 따라 분할 비율 자동 조정
- **Cascade 블렌딩**: 경계 영역에서 두 cascade를 블렌딩하여 전환 더 부드럽게
- **PCSS (Percentage Closer Soft Shadows)**: 거리에 따라 그림자 부드러움 변화 (현실적인 반영)
- **Variance Shadow Maps (VSM)**: PCF 대신 VSM으로 부드러운 그림자와 성능 개선
- **Multi-Light 지원**: Point Light, Spot Light를 위한 Cube Shadow Map 추가
- **설정 외부화**: Cascade 개수, 분할 비율, 텍스처 크기를 설정 파일로 관리
