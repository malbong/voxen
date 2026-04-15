# Cloud Rendering

<img width="800" height="460" alt="Image" src="https://github.com/user-attachments/assets/a190f7cf-1068-43e7-8413-6586e5f6f61e" />

<img width="800" height="460" alt="Image" src="https://github.com/user-attachments/assets/03d98e6b-7280-411a-ab34-aca3a2368a5a" />

<img width="800" height="460" alt="Image" src="https://github.com/user-attachments/assets/d43d3b66-5905-4703-a5b5-c1335ac384fd" />

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
m_worldPosition.x -= m_speed * dt;  // 초당 10블록 속도로 -X 방향 이동
```

**카메라 추적:**

카메라가 한 셀(16블록) 이상 이동하면 맵 중심을 셀 단위로 `m_worldPosition`을 재조정하고
`m_mapDataOffset`을 함께 갱신하여 dataMap 샘플링 위치를 변경한 뒤 `BuildCloud()`로 메시를 업데이트한다.

```
카메라와 맵 중심의 차이(diffPos) 계산
├─ diffPos.x > CLOUD_SCALE_SIZE  → 맵 중심을 +X로 한 칸 이동
├─ diffPos.z > CLOUD_SCALE_SIZE  → 맵 중심을 +Z로 한 칸 이동
├─ diffPos.x < 0                 → 맵 중심을 -X로 한 칸 이동
└─ diffPos.z < 0                 → 맵 중심을 -Z로 한 칸 이동

if (diffPos.x > CLOUD_SCALE_SIZE) {
        newWorldPosition.x += CLOUD_SCALE_SIZE;
        m_offsetPosition.x += CLOUD_SCALE_SIZE;
}

...

if (newWorldPosition != m_worldPosition) {
        m_worldPosition = newWorldPosition;
        BuildCloud();
}
```

### 4.3 타일링 (무한 반복)

`m_offsetPosition`을 셀 크기에 맞춰 스케일링 후 moduler 연산으로 적절히 무한타일링을 진행

```cpp
const int HALF_CLOUD_MAP_SIZE = CLOUD_MAP_SIZE / 2;
const int GRID_OFFSET_X = (int)m_offsetPosition.x / CLOUD_SCALE_SIZE;
const int GRID_OFFSET_Z = (int)m_offsetPosition.z / CLOUD_SCALE_SIZE;

for (int i = 0; i < CLOUD_MAP_SIZE; ++i) {
        for (int j = 0; j < CLOUD_MAP_SIZE; ++j) {
                int x = (GRID_OFFSET_X + i - HALF_CLOUD_MAP_SIZE) % CLOUD_DATA_MAP_SIZE;
                int z = (GRID_OFFSET_Z + j - HALF_CLOUD_MAP_SIZE) % CLOUD_DATA_MAP_SIZE;

                if (x < 0)
                        x += CLOUD_DATA_MAP_SIZE;
                if (z < 0)
                        z += CLOUD_DATA_MAP_SIZE;

                m_map[i][j] = m_dataMap[x][z];
        }
}
```

### 4.4 렌더링 파이프라인

물 내외부 판단 후 그리기 순서를 조정

- 물 외부인 경우: 마지막에 그려서 Z에 상관없이 반투명 유지
- 물 내부인 경우: 물 표면을 마지막에 그려 멀리 있는 구름과 물 표면의 투명도를 유지

```
[물 밖]
1. RenderMirrorWorld()
2. RenderWaterPlane()
3. RenderFogFilter()
4. RenderSkybox()
*5. RenderCloud()        → 구름 (cloudPSO)

[물 안]
1. RenderFogFilter()
2. RenderSkybox()
*3. RenderCloud()
4. RenderWaterPlane()
```

### 4.5 Blend State 설정

반투명 구름 렌더링을 위한 BS 설정

```
// alphaBS
desc.AlphaToCoverageEnable = false;
desc.IndependentBlendEnable = false;
desc.RenderTarget[0].BlendEnable = true;
desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
...
cloudPSO.blendState = alphaBS;
```

Blend State 설정으로 얻는 결과

```
→ FinalColor = SrcColor * SrcAlpha + DestColor * (1 - SrcAlpha)
```

옵션 `AlphaToCoverageEnable = false`

- 서브샘플이 PS에서 뱉는 Alpha값을 평균처리 후 서브샘플에 대해서 값을 양자화하여 Blending되는 옵션
  - Ex) 알파의 평균이 `0.249`이면 `0.25`로 양자화되어 Blending
  - Ex) 알파의 평균이 `0.251`이면 `0.5`로 양자화되어 Blending
  - 그 결과 구간이 나뉘어지는 문제 발생 -> 프로젝트 환경에서는 단순히 옵션을 끄고 진행
    <img width="800" height="460" alt="Image" src="https://github.com/user-attachments/assets/4c986217-ebf6-496a-9f4b-e3854043fc37" />
- 철조망 렌더링 같이 얇은 메쉬의 MSAA 렌더링을 위한 옵션
  - 맺히지 않는 부분은 Clip → Alpha 0
  - 맺히는 부분은 그대로 렌더링 → Alpha 1
  - 평균적으로 서브샘플의 개수에 따라 평균 Alpha값이 양자화되어 섞일 수 있음

### 4.6 구름 색상 결정 Pixel Shader

Pixel Shader는 구름 픽셀의 최종 색상(RGB)을 4단계에 걸쳐 결정한다. 구름은 텍스처 없이 색상을 계산하므로, 모든 색상 변화가 셰이더 내에서 절차적으로 만들어진다.

**[1단계] 기본 색상 (volumeColor)**

구름의 기본 색상은 Constant Buffer에서 전달되는 `volumeColor`로, 흰색(1,1,1)으로 설정된다. 텍스처 샘플링 없이 단색을 사용하는 것은 복셀 스타일의 구름에서 충분하며, 이후 단계의 조명과 블렌딩이 색상 변화를 만들어낸다.

**[2단계] 태양 방향에 따른 Horizon Color 선택**

```hlsl
float sunAniso = max(dot(lightDir, eyeDir), 0.0);
float3 eyeHorizonColor = lerp(normalHorizonColor, sunHorizonColor, sunAniso);
```

하늘과 맞닿는 구름 가장자리에 어떤 색을 입힐지를 시선 방향과 태양 방향의 관계로 결정한다.

| 조건                       | sunAniso   | 결과                             |
| -------------------------- | ---------- | -------------------------------- |
| 태양 반대 방향을 바라볼 때 | 0에 가까움 | `normalHorizonColor` (차가운 톤) |
| 태양 방향을 바라볼 때      | 1에 가까움 | `sunHorizonColor` (따뜻한 톤)    |

이 방향별 색상 차이가 구름에 시간대(일출/일몰)와 방향에 따른 색감 변화를 부여한다.

**[3단계] 거리(distance) 기반 Horizon Color 블렌딩**
카메라에서 가까운 구름은 본래 흰색을 유지하고, 먼 구름일수록 2단계에서 결정한 horizon color로 전환된다.

- maxRenderDistance 이내의 거리는 기본색(`volumeColor`)로, 그보다 멀어지면 horizon color로 보간

```hlsl
float distance = length(input.posWorld.xz - eyePos.xz);
float clampedDistance = clamp(distance, maxRenderDistance, cloudScale);
float horizonWeight = smoothstep(maxRenderDistance, cloudScale, clampedDistance);
float albedo = lerp(volumeColor, eyeHorizonColor, horizonWeight);
```

```
가까운 구름 ─> maxRenderDistance ─────────── cloudScale <─ 먼 구름
[          기본색              ][   Lerp   ][ 태양을 방향에 따른 Horizon 색]
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

### 4.7 투명 처리

구름은 완전 불투명도 완전 투명도 아닌 반투명 오브젝트다. Alpha Blending과 거리 기반 Alpha 감쇄를 결합하여, 구름이 하늘과 자연스럽게 섞이면서 먼 곳에서는 서서히 사라지도록 처리한다.

**Alpha 값 결정 (PS):**

```hlsl
float distance = length(input.posWorld.xz - eyePos.xz);
    float clampedDistance = clamp(distance, maxRenderDistance, cloudScale);
float alphaWeight = 1.0 - smoothstep(maxRenderDistance, cloudScale, clampedDistance);
float alpha = alphaWeight * 0.75; // [0, 0.75]
```

| 거리 구간                                 | alphaWeight | alpha      | 의미                   |
| ----------------------------------------- | ----------- | ---------- | ---------------------- |
| distance ≤ maxRenderDistance              | 1.0         | 0.75       | 가장 불투명 (최대 75%) |
| maxRenderDistance < distance < cloudScale | 0.0 ~ 1.0   | 0.0 ~ 0.75 | 서서히 투명해짐        |
| distance ≥ cloudScale                     | 0.0         | 0.0        | 완전 투명              |

최대 alpha를 0.75로 제한한 이유는 구름이 완전 불투명하면 하늘을 가려 답답한 느낌을 주기 때문이다.

## 5. 참고

### 면 컬링으로 메시 최소화

인접 셀 검사로 내부 면을 제거한다. 64x64 맵에서 모든 셀이 구름이라면 셀당 6면 = 24,576면이지만, 인접 면 제거 후에는 테두리와 상하면만 남아 면 수가 크게 감소한다. 단, 블록 메시와 달리 Greedy Meshing은 적용하지 않았는데, 구름 셀 수가 청크에 비해 적어 효과 대비 복잡도가 맞지 않기 때문이다.

### 셀 단위 카메라 추적

카메라가 이동할 때마다 메시를 재생성하면 비용이 크므로, CLOUD_SCALE_SIZE(16블록) 단위로 맵 중심을 이동한다. 즉 카메라가 16블록 이상 이동해야 `BuildCloud()`가 호출되어, 소규모 이동에서는 메시 재생성이 발생하지 않는다.

### GPU 버퍼 업데이트 방식

`BuildCloud()` 호출 시 버퍼크기를 조절 및 Update하여 메쉬를 렌더링한다.

```
// BuildCloud() 의 일부...
if (!DXUtils::ResizeBuffer(m_vertexBuffer, m_vertices, D3D11_BIND_VERTEX_BUFFER)) ...
if (!DXUtils::UpdateBuffer(m_vertexBuffer, m_vertices)) ...
```

## 6. 회고

- 카메라 이동과 구름 자체의 이동 두가지를 고려해서 셀을 재정의하는데 고전을 했던 챕터
  - OffsetPosition과 WorldPosition의 처리
  - 셀처리 방식의 모듈 연산
- 구름 셀 Noise Map을 시각화하기 위한 추가적 렌더링이 필요하기도 했음
- 반투명 렌더링을 어떻게 진행할지 다양한 테스트를 진행
  - 여러가지 BS를 구성하여 렌더링 테스트
  - 후처리를 이용한 반투명 렌더링 고려도 해봄
  - BackFace를 Culling을 할지, 메쉬를 구성하지 말아야 할지 여러 고민이 필요했음
- PS에서 색상 결정은 의외로 간단하게 구성할 수 있었음
  - PBR 로직이 구성되고 나서는 더 편해진 면이 있었음
  - Forward Rendering에서도 PBR 라이팅 모델을 사용하여 라이팅 로직이 Common.hlsli에 들어간게 아쉬움
