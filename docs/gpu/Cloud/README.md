# Cloud Rendering

## 1. 개요

구름은 노이즈 기반 2D 그리드를 활용하여 복셀 스타일 메시를 생성하고, Forward 렌더링 패스에서 Alpha Blending으로 반투명하게 그리는 시스템이다.
일정 높이(192)에 떠 있는 구름이 바람에 의해 한 방향으로 이동하며, 카메라를 따라 무한하게 펼쳐지는 것처럼 보이도록 타일링 방식으로 맵을 반복 참조한다.

## 2. 도입 동기

Minecraft 스타일의 플랫 복셀 구름을 채택하여, 최소한의 연산으로 복셀 월드에 어울리는 구름을 구현했다.

## 3. 핵심 아이디어

### 3.1 2단계 맵 구조

구름의 형태를 결정하는 데이터 맵과, 실제로 메시를 생성하는 뷰포트 맵을 분리했다.

| 맵          | 크기      | 역할                                                       |
| ----------- | --------- | ---------------------------------------------------------- |
| `m_dataMap` | 512 x 512 | Perlin FBM 노이즈로 구름 존재 여부를 미리 계산한 전체 맵   |
| `m_map`     | 64 x 64   | 카메라 주변을 기준으로 dataMap에서 샘플링한 현재 뷰포트 맵 |

`m_dataMap`은 초기화 시 한 번만 생성하고, 카메라가 이동하면 `m_map`만 `m_dataMap`에서 다시 샘플링하여 메시를 재생성한다.

### 3.2 노이즈 기반 형태 생성

두 가지 스케일의 Perlin FBM 노이즈를 조합하여 구름 형태를 결정한다.

```
noise1 = PerlinFbm(i/512, j/512, freq=64, octaves=3)   → 큰 덩어리 (threshold > 0.2)
noise2 = PerlinFbm(i/512, j/512, freq=256, octaves=1)   → 작은 조각 (threshold > 0.45)
dataMap[i][j] = noise1 > 0.2 || noise2 > 0.45
```

- noise1: 낮은 주파수(64), 3옥타브로 크고 뭉쳐진 구름 덩어리 생성
- noise2: 높은 주파수(256), 1옥타브로 흩어진 작은 구름 조각 생성
- OR 조합으로 두 패턴을 합쳐 다양한 크기의 구름 형태를 만듦

<img width="640" height="640" alt="Image" src="https://github.com/user-attachments/assets/00037e00-e422-4e13-8dd0-62c900f66c65" />

### 3.3 거리 기반 투명도 페이드아웃

구름은 카메라에서 멀어질수록 투명해져 자연스럽게 사라진다.
청크가 렌더링 되는 `maxRenderDistance`에서는 `alpha`를 `0.75`, 그 외의 멀리 있는 구름은 `0.0`에 가깝게 알파값을 조정하여 블랜딩한다.

- maxRenderDistance ~ cloudScale 구간에서 alpha가 0.75 → 0으로 감소
- 최대 불투명도가 0.75로 제한되어 항상 반투명한 느낌을 유지

```
// distance 범위 clamp
float distance = length(input.posWorld.xz - eyePos.xz);
float clampedDistance = clamp(distance, maxRenderDistance, cloudScale);

// distance alpha
float alphaWeight = 1.0 - smoothstep(maxRenderDistance, cloudScale, clampedDistance);
float alpha = alphaWeight * 0.75; // [0, 0.75]
```

</br>

## 4. 구현 내용

### 4.1 메시 생성 (BuildCloud)

뷰포트 맵(64x64)을 순회하며 인접 셀과의 관계를 검사하여 면 단위로 메시를 생성한다.

```
m_map[64][64] 순회:
├─ m_map[i][j] == false → 건너뜀
├─ 인접 4방향(x_n, x_p, z_n, z_p) 검사 → 인접 셀이 구름이면 해당 면 제거
├─ 상하면(y_n, y_p)은 항상 생성 (1칸 높이이므로 위아래는 항상 노출)
└─ MeshGenerator::CreateCloudMesh() → 노출면만 버텍스/인덱스 생성
```

**CloudVertex 구조:**

| 필드       | 타입    | 용도                                         |
| ---------- | ------- | -------------------------------------------- |
| `position` | Vector3 | 로컬 공간 꼭짓점 좌표 (0~1 단위 큐브)        |
| `face`     | uint8_t | 면 방향 인덱스 (0~5: -X, +X, -Y, +Y, -Z, +Z) |

구름 그리드의 각 셀은 1x1x1 단위이며, World Matrix의 Scale로 실제 크기(16x4x16)로 변환된다.

### 4.2 이동과 카메라 추적

구름은 두 가지 움직임을 가진다:

**바람에 의한 이동:**

```cpp
m_mapCenterWorldPosition.x -= m_speed * dt;  // 초당 10블록 속도로 -X 방향 이동
```

**카메라 추적:**

```
카메라와 맵 중심의 차이(diffPos) 계산
├─ diffPos.x > CLOUD_SCALE_SIZE  → 맵 중심을 +X로 한 칸 이동
├─ diffPos.z > CLOUD_SCALE_SIZE  → 맵 중심을 +Z로 한 칸 이동
├─ diffPos.x < 0                 → 맵 중심을 -X로 한 칸 이동
└─ diffPos.z < 0                 → 맵 중심을 -Z로 한 칸 이동

if (diffPos.x > CLOUD_SCALE_SIZE) {
        newMapCenterWorldPosition.x += CLOUD_SCALE_SIZE;
        m_mapDataOffset.x += CLOUD_SCALE_SIZE;
}
```

카메라가 한 셀(16블록) 이상 이동하면 맵 중심을 셀 단위로 재조정하고, `m_mapDataOffset`을 함께 갱신하여 dataMap 샘플링 위치를 변경한 뒤 `BuildCloud()`로 메시를 업데이트한다.

### 4.3 타일링 (무한 반복)

```cpp
int x = ((int)(m_mapDataOffset.x / CLOUD_SCALE_SIZE) + i - (int)(CLOUD_MAP_SIZE * 0.5f))
        % CLOUD_DATA_MAP_SIZE;
if (x < 0) x += CLOUD_DATA_MAP_SIZE;
```

dataMap(512x512)을 모듈러 연산으로 참조하므로, 카메라가 아무리 멀리 이동해도 인덱스가 0~511 범위에서 순환한다. 이를 통해 구름 패턴이 끊김 없이 무한 반복된다.

### 4.4 렌더링 파이프라인

**렌더 순서 (Forward Pass):**

```
[수면 위일 때]
1. RenderMirrorWorld()  → 수면 반사용 렌더링 (cloudMirrorPSO)
2. RenderWaterPlane()   → 수면 렌더링
3. RenderFogFilter()    → 안개 필터
4. RenderSkybox()       → 스카이박스
5. RenderCloud()        → 구름 (cloudPSO)

[수면 아래일 때]
1. RenderFogFilter()
2. RenderSkybox()
3. RenderCloud()
4. RenderWaterPlane()
```

스카이박스 뒤, 불투명 오브젝트 이후에 그려진다. 반투명 오브젝트이므로 depth test는 켜져 있지만 뒤쪽에 위치한 스카이박스와 자연스럽게 블렌딩된다.

### 4.6 PSO 구성

**cloudPSO:**

```
cloudPSO = basicPSO 기반
├─ InputLayout  : cloudIL (Position + Face)
├─ VertexShader : CloudVS.hlsl
├─ PixelShader  : CloudPS.hlsl
└─ BlendState   : alphaBS (SrcAlpha / InvSrcAlpha 블렌딩)
```

**Alpha Blend State (alphaBS):**

```
SrcBlend      = SRC_ALPHA
DestBlend     = INV_SRC_ALPHA
BlendOp       = ADD
→ FinalColor = SrcColor * SrcAlpha + DestColor * (1 - SrcAlpha)
```

**cloudMirrorPSO:**

```
cloudMirrorPSO = cloudPSO 기반
├─ RasterizerState    : mirrorRS (반전된 와인딩 오더)
├─ DepthStencilState  : mirrorDrawMaskedDSS (스텐실 마스크 기반)
└─ StencilRef         : 1 (수면 영역만 렌더링)
```

수면 반사 렌더링에서는 스텐실 마스크로 수면 영역만 통과시키고, 래스터라이저의 와인딩 오더를 반전하여 반사된 지오메트리를 올바르게 렌더링한다.

### 4.7 PS 색상 결정 파이프라인

Pixel Shader는 구름 픽셀의 최종 색상(RGB)을 4단계에 걸쳐 결정한다. 구름은 텍스처 없이 색상을 계산하므로, 모든 색상 변화가 셰이더 내에서 절차적으로 만들어진다.

**[1단계] 기본 색상 (volumeColor)**

```hlsl
float3 albedo = volumeColor;  // (1.0, 1.0, 1.0) = 흰색
```

구름의 기본 색상은 Constant Buffer에서 전달되는 `volumeColor`로, 흰색(1,1,1)으로 설정된다. 텍스처 샘플링 없이 단색을 사용하는 것은 복셀 스타일의 구름에서 충분하며, 이후 단계의 조명과 블렌딩이 색상 변화를 만들어낸다.

**[2단계] 태양 방향에 따른 Horizon Color 선택**

```hlsl
float sunAniso = max(dot(lightDir, eyeDir), 0.0);
float3 eyeHorizonColor = lerp(normalHorizonColor, sunHorizonColor, sunAniso);
```

하늘과 맞닿는 구름 가장자리에 어떤 색을 입힐지를 시선 방향과 태양 방향의 관계로 결정한다. `lightDir`과 `eyeDir`의 내적은 플레이어가 태양 쪽을 바라보는 정도를 나타내며, 이 값에 따라 두 가지 horizon color를 보간한다.

| 조건                       | sunAniso   | 결과                             |
| -------------------------- | ---------- | -------------------------------- |
| 태양 반대 방향을 바라볼 때 | 0에 가까움 | `normalHorizonColor` (차가운 톤) |
| 태양 방향을 바라볼 때      | 1에 가까움 | `sunHorizonColor` (따뜻한 톤)    |

이 방향별 색상 차이가 구름에 시간대(일출/일몰)와 방향에 따른 색감 변화를 부여한다.

**[3단계] 거리 기반 Horizon Color 블렌딩**

```hlsl
float distance = length(input.posWorld.xz - eyePos.xz);
float horizonWeight = smoothstep(maxRenderDistance, cloudScale, clamp(distance, maxRenderDistance, cloudScale));
albedo = lerp(albedo, eyeHorizonColor, horizonWeight);
```

카메라에서 가까운 구름은 본래 흰색을 유지하고, 먼 구름일수록 2단계에서 결정한 horizon color로 전환된다. `smoothstep`으로 `maxRenderDistance`부터 `cloudScale`(= 64 _ 16 _ 0.5 = 512) 구간에서 부드럽게 보간한다.

```
가까운 구름 ─── maxRenderDistance ─────── cloudScale ─── 먼 구름
 albedo=흰색       ↓ 서서히 변화 ↓          albedo=horizonColor
```

이로 인해 먼 구름이 하늘의 수평선 색과 자연스럽게 합쳐지면서, 구름 메시의 끝자락이 하늘과 뚜렷하게 구분되지 않게 된다.

**[4단계] PBR 조명 적용**

```hlsl
float3 normal = getNormal(input.face);  // face 인덱스 → 면 노멀 벡터
float3 ambientLighting = getAmbientLighting(1.0, albedo, position, normal, 0.0, 0.75);
float3 directLighting  = getDirectLighting(normal, position, albedo, 0.0, 0.75, false);
```

3단계까지 결정된 albedo에 PBR 조명을 적용하여 최종 RGB를 산출한다.

| 파라미터  | 값    | 의미                                                                         |
| --------- | ----- | ---------------------------------------------------------------------------- |
| AO        | 1.0   | 차폐 없음 (구름은 평면이므로 주변 차폐가 의미 없음)                          |
| metallic  | 0.0   | 비금속 - 구름은 빛을 반사하지 않고 산란시킴                                  |
| roughness | 0.75  | 높은 거칠기 - 확산 반사 위주의 부드러운 외관                                 |
| useShadow | false | 그림자 미수신 - 구름은 월드보다 높이 있어 다른 오브젝트의 그림자를 받지 않음 |

노멀은 텍스처 없이 면 방향(face)으로 결정되므로, 면마다 균일한 조명이 적용된다. 윗면(+Y)은 태양빛을 직접 받아 밝고, 아랫면(-Y)은 어두워져 자연스러운 명암 대비가 생긴다.

최종 RGB = `ambientLighting + directLighting`

### 4.8 투명 처리

구름은 완전 불투명도 완전 투명도 아닌 반투명 오브젝트다. Alpha Blending과 거리 기반 Alpha 감쇄를 결합하여, 구름이 하늘과 자연스럽게 섞이면서 먼 곳에서는 서서히 사라지도록 처리한다.

**Alpha 값 결정 (PS):**

```hlsl
float alphaWeight = smoothstep(maxRenderDistance, cloudScale, clamp(distance, maxRenderDistance, cloudScale));
float alpha = (1.0 - alphaWeight) * 0.75;
```

| 거리 구간                                 | alphaWeight | alpha      | 의미                   |
| ----------------------------------------- | ----------- | ---------- | ---------------------- |
| distance ≤ maxRenderDistance              | 0.0         | 0.75       | 가장 불투명 (최대 75%) |
| maxRenderDistance < distance < cloudScale | 0.0 ~ 1.0   | 0.75 ~ 0.0 | 서서히 투명해짐        |
| distance ≥ cloudScale                     | 1.0         | 0.0        | 완전 투명              |

최대 alpha를 0.75로 제한한 이유는 구름이 완전 불투명하면 하늘을 가려 답답한 느낌을 주기 때문이다. 75% 불투명도는 구름 뒤로 하늘이 살짝 비치면서도 구름의 존재감을 충분히 유지하는 균형점이다.

**Alpha Blend State (GPU):**

```
Output.RGB = Src.RGB * Src.A  +  Dest.RGB * (1 - Src.A)
Output.A   = Src.A  * 1      +  Dest.A   * 1
```

| 설정      | 값              | 역할                                     |
| --------- | --------------- | ---------------------------------------- |
| SrcBlend  | `SRC_ALPHA`     | 구름 색상에 자신의 alpha를 곱함          |
| DestBlend | `INV_SRC_ALPHA` | 기존 화면(스카이박스)에 (1-alpha)를 곱함 |
| BlendOp   | `ADD`           | 두 결과를 더함                           |

예를 들어 가까운 구름(alpha=0.75)이 스카이박스 위에 그려지면:

```
최종 색 = 구름색 × 0.75 + 스카이박스색 × 0.25
```

**렌더 순서가 중요한 이유:**

Alpha Blending은 이미 그려진 픽셀(Dest)과 새로 그리는 픽셀(Src)을 섞는 방식이므로, **뒤에 있는 오브젝트가 먼저 그려져야** 한다.

```
1. Deferred Pass    → 불투명 지형/블록 (G-Buffer)
2. RenderSkybox()   → 하늘 (Dest에 기록됨)
3. RenderCloud()    → 구름 (Src로서 Dest인 스카이박스와 블렌딩)
```

구름이 스카이박스보다 먼저 그려지면 Dest가 비어있어 블렌딩 결과가 어두워진다. 스카이박스를 먼저 그려서 Dest를 채운 뒤 구름을 블렌딩하는 순서가 올바른 반투명 합성의 핵심이다.

**Depth Test와의 관계:**

구름은 Depth Test가 켜진 상태로 렌더링된다. 이는 지형 앞에 있는 구름만 그려지고, 산 뒤에 가려지는 구름은 자동으로 차단됨을 의미한다. 단, Depth Write는 기본 상태(켜짐)이므로 구름이 depth에 기록되어 이후 그려지는 투명 오브젝트에 영향을 줄 수 있다. 구름이 y=192에 위치하여 대부분의 오브젝트보다 높기 때문에 실질적으로 문제가 되지 않는다.

## 5. 구현 특징

### 면 컬링으로 메시 최소화

인접 셀 검사로 내부 면을 제거한다. 64x64 맵에서 모든 셀이 구름이라면 셀당 6면 = 24,576면이지만, 인접 면 제거 후에는 테두리와 상하면만 남아 면 수가 크게 감소한다. 단, 블록 메시와 달리 Greedy Meshing은 적용하지 않았는데, 구름 셀 수가 청크에 비해 적어 효과 대비 복잡도가 맞지 않기 때문이다.

### 셀 단위 카메라 추적

카메라가 이동할 때마다 메시를 재생성하면 비용이 크므로, CLOUD_SCALE_SIZE(16블록) 단위로 맵 중심을 이동한다. 즉 카메라가 16블록 이상 이동해야 `BuildCloud()`가 호출되어, 소규모 이동에서는 메시 재생성이 발생하지 않는다.

### GPU 버퍼 재생성 방식

`BuildCloud()` 호출 시 기존 Vertex/Index/Constant Buffer를 모두 `Reset()` 후 새로 생성한다. 구름 메시는 카메라 이동 시에만 재생성되므로 빈도가 낮아 동적 버퍼 대신 매번 새 버퍼를 생성하는 방식을 택했다.

## 6. 회고

- 현재 Greedy Meshing을 적용하지 않아 동일 높이의 인접 면이 각각 개별 쿼드로 존재한다. 구름 맵이 커지면 버텍스 수가 비례하여 증가하므로, 필요 시 Greedy Meshing으로 면을 병합하면 드로우 콜 비용을 줄일 수 있다.
- 구름이 그림자를 월드에 드리우지 않는다. 태양 방향으로 구름 맵을 투영하여 섀도우 맵에 반영하면 더 사실적인 연출이 가능하다.
- 현재 1칸 높이의 플랫 구름이므로 입체감이 부족하다. 노이즈 값의 강도에 따라 높이를 달리하여 다층 구름을 구현하면 볼륨감을 향상시킬 수 있다.
- `BuildCloud()` 시 버퍼를 매번 새로 생성/해제하는데, 최대 크기 버퍼를 미리 할당하고 `UpdateBuffer()`로 데이터만 갱신하면 메모리 할당 비용을 줄일 수 있다.
