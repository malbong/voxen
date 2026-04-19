# Frustum Culling

## <img width="1914" height="1075" alt="Image" src="https://github.com/user-attachments/assets/e21ac2ee-6d11-42c2-844b-df6d729c19dc" />

<img width="955" height="538" alt="Image" src="https://github.com/user-attachments/assets/2f6eb2be-9c92-4e7d-a966-d44e35db4f52" />

## 1. 개요

Frustum Culling은 카메라의 시야 절두체(frustum) 밖에 있는 청크를 렌더링 대상에서 제외하는 기법이다.
화면에 보이지 않는 청크의 드로우 콜을 제거하여 GPU 부하를 줄이는 핵심 최적화다.
매 프레임 로드된 모든 청크를 대상으로 3종류의 프러스텀 검사를 수행한다.

1. 단순한 카메라 렌더 리스트 컬링
2. 쉐도우 맵을 위한 Light 렌더 리스트 컬링
3. 수면 반사를 위한 렌더 리스트 컬링

## 2. 도입 동기

복셀 월드에서 렌더 거리 내 로드된 청크 수는 수백 개에 달한다.
이 중 카메라 시야에 실제로 들어오는 청크는 일부에 불과하지만, 컬링 없이 모두 GPU에 보내면 불필요한 버텍스 처리와 래스터라이징이 발생한다.
프러스텀 컬링으로 보이지 않는 청크를 CPU 단계에서 걸러내면, GPU에 전달되는 드로우 콜 수를 대폭 줄일 수 있다.

추가로 섀도우 맵과 수면 반사도 각각 별도의 프러스텀을 가지므로, 이들에 대해서도 독립적인 컬링이 필요하다.

## 3. 핵심 아이디어

### 3.1 NDC 역변환 기반 프러스텀 구성

일반적인 프러스텀 컬링은 뷰-프로젝션 행렬에서 6개 평면 방정식을 직접 추출하는 방식을 사용한다. (Gribb-Hartmann Culling)
이 프로젝트에서는 비효율적이지만 직관적인 역변환 형태의 방식을 채용했다.
디버깅이 쉽고, 기하적으로 이해하기 쉬운 코드를 작성하기 위함이다.

```
NDC 큐브의 8개 꼭짓점 → (View × Projection)⁻¹ 역변환 → 월드 공간 8개 꼭짓점 → 6개 평면 구성
```

NDC 공간의 단위 큐브 꼭짓점을 월드 공간으로 역변환하면 프러스텀의 실제 월드 좌표를 얻고, 이 꼭짓점들로부터 6개 평면을 구성하는 방식이다.
이 방식은 카메라 프러스텀뿐 아니라 캐스케이드 섀도우의 직교 프러스텀에도 동일한 코드로 적용할 수 있다는 장점이 있다.

### 3.2 AABB 보수적 판정

청크는 32x32x32 크기의 축 정렬 바운딩 박스(AABB)로 근사된다.

1. AABB의 8개 꼭짓점 모두가 어느 한 평면의 바깥에 있어야만 해당 청크를 제외한다.
2. 꼭짓점 하나라도 안쪽에 있으면 보수적으로 통과시킨다.
   이는 거짓 음성(보이는데 제외하는 경우)을 방지하는 대신, 거짓 양성(안 보이는데 통과-불필요한 드로우콜 발생하는 경우)을 허용하는 전략이다.

## 4. 구현 내용

### 4.1 렌더 리스트 구성 (UpdateRenderChunkList)

매 프레임 `m_chunkMap`의 모든 로드된 청크를 순회하며 3가지 프러스텀 검사를 수행한다.

```
m_chunkMap 순회 (로드 완료 + 비어있지 않은 청크만):
│
├─ [카메라 프러스텀] FrustumCulling(chunkPos, useMirror=false, useShadow=false)
│   → 통과 시 m_renderChunkList에 추가
│
├─ [섀도우 프러스텀] CASCADE_NUM(3)개 캐스케이드 중 하나라도 통과하면
│   FrustumCulling(chunkPos, useShadow=true, index=0~2)
│   → 통과 시 m_renderShadowChunkList에 추가 (break로 중복 방지)
│
└─ [미러 프러스텀] 청크 위치를 미러 평면으로 반사 변환 후
    mirrorChunkPos = Transform(chunkPos, mirrorPlaneMatrix)
    FrustumCulling(mirrorChunkPos, useMirror=true, useShadow=false)
    → 통과 시 m_renderMirrorChunkList에 추가
```

| 렌더 리스트               | 프러스텀 소스                        | 용도                                            |
| ------------------------- | ------------------------------------ | ----------------------------------------------- |
| `m_renderChunkList`       | 카메라 View × Projection             | 메인 렌더링 (Opaque + SemiAlpha + Transparency) |
| `m_renderShadowChunkList` | Light View × Cascade Projection[0~2] | 3개 캐스케이드 섀도우 맵                        |
| `m_renderMirrorChunkList` | 카메라 프러스텀 + 미러 평면 반사     | 수면 반사 렌더링                                |

### 4.2 프러스텀 평면 구성

```cpp
// 1. 역행렬 계산
Matrix invMat;
if (useShadow)
    invMat = (light.GetViewMatrix() * light.GetProjectionMatrixFromCascade(index)).Invert();
else
    invMat = (camera.GetViewMatrix() * camera.GetProjectionMatrix()).Invert();
```

프러스텀 종류에 따라 사용하는 행렬이 다르다.

| 프러스텀            | View 행렬   | Projection 행렬                    |
| ------------------- | ----------- | ---------------------------------- |
| 카메라 / 미러       | Camera View | Camera Perspective Projection      |
| 섀도우 캐스케이드 i | Light View  | Cascade Orthographic Projection[i] |

- cf. 미러는 Camera View를 그대로 사용하고, 물체 `position`이 수면에 대한 반사 행렬에 곱해져 들어온다.

```cpp
// 2. NDC 8개 꼭짓점 → 월드 공간 역변환
worldPos[0] = Transform((-1, +1, 0), invMat)   // Near Top-Left
worldPos[1] = Transform((+1, +1, 0), invMat)   // Near Top-Right
worldPos[2] = Transform((+1, -1, 0), invMat)   // Near Bottom-Right
worldPos[3] = Transform((-1, -1, 0), invMat)   // Near Bottom-Left
worldPos[4] = Transform((-1, +1, 1), invMat)   // Far Top-Left
worldPos[5] = Transform((+1, +1, 1), invMat)   // Far Top-Right
worldPos[6] = Transform((+1, -1, 1), invMat)   // Far Bottom-Right
worldPos[7] = Transform((-1, -1, 1), invMat)   // Far Bottom-Left
```

NDC에서 Z=0은 Near 평면, Z=1은 Far 평면이다 (DirectX 좌표계). 이 8개 점을 역변환하면 월드 공간에서의 프러스텀 꼭짓점이 된다.

```cpp
// 3. 8개 꼭짓점으로 6개 평면 생성
vfPlanes[0] = XMPlaneFromPoints(worldPos[0], worldPos[1], worldPos[2])  // Near
vfPlanes[1] = XMPlaneFromPoints(worldPos[7], worldPos[6], worldPos[5])  // Far
vfPlanes[2] = XMPlaneFromPoints(worldPos[4], worldPos[5], worldPos[1])  // Top
vfPlanes[3] = XMPlaneFromPoints(worldPos[3], worldPos[2], worldPos[6])  // Bottom
vfPlanes[4] = XMPlaneFromPoints(worldPos[4], worldPos[0], worldPos[3])  // Left
vfPlanes[5] = XMPlaneFromPoints(worldPos[1], worldPos[5], worldPos[6])  // Right
```

### 4.3 AABB vs 프러스텀 판정

```cpp
float x = (float)Chunk::CHUNK_SIZE;  // 32
float y = (float)Chunk::CHUNK_SIZE;  // 32
float z = (float)Chunk::CHUNK_SIZE;  // 32
if (useMirror)
    y *= -1;  // 미러 반사 시 Y축 반전
```

청크의 AABB는 `position`을 원점으로 `(position + 32, position + 32, position + 32)`까지의 축 정렬 박스다.
미러 렌더링 시에는 Y축이 반전되어 `position.y - 32` 방향으로 확장된다.

```cpp
for (int i = 0; i < vfPlanes.size(); ++i) {
    if (PlaneDotCoord(vfPlanes[i], position)                       <= 0) continue; // 한 점이 평면 내부면 다음 평면 판단
    if (PlaneDotCoord(vfPlanes[i], position + (x, 0, 0))           <= 0) continue;
    if (PlaneDotCoord(vfPlanes[i], position + (0, y, 0))           <= 0) continue;
    if (PlaneDotCoord(vfPlanes[i], position + (x, y, 0))           <= 0) continue;
    if (PlaneDotCoord(vfPlanes[i], position + (0, 0, z))           <= 0) continue;
    if (PlaneDotCoord(vfPlanes[i], position + (x, 0, z))           <= 0) continue;
    if (PlaneDotCoord(vfPlanes[i], position + (0, y, z))           <= 0) continue;
    if (PlaneDotCoord(vfPlanes[i], position + (x, y, z))           <= 0) continue;
    return false;  // 8개 꼭짓점 모두 이 평면 바깥 → 프러스텀 밖
}
return true;  // 어느 평면에서도 완전히 밖이 아님 → 프러스텀 안(또는 교차)
```

**판정 로직:**

`XMPlaneDotCoord(plane, point)`는 점이 평면의 안쪽(음수)인지 바깥쪽(양수)인지를 반환한다.

```
PlaneDotCoord 결과:
  ≤ 0  →  점이 프러스텀 안쪽 (또는 평면 위)  →  continue (다음 꼭짓점 검사)
  > 0  →  점이 프러스텀 바깥쪽                →  다음 꼭짓점으로
```

6개 평면 각각에 대해:

1. AABB의 8개 꼭짓점을 순회
2. **하나라도** 평면 안쪽(≤ 0)이면 → `continue`로 해당 평면 통과 (다음 평면 검사)
3. **8개 모두** 바깥쪽(> 0)이면 → `return false` (프러스텀 밖으로 확정, 컬링)

6개 평면을 모두 통과하면 `return true` (프러스텀 안에 있거나 교차).

## 5. 3가지 프러스텀 비교

### 5.1 카메라 프러스텀

```
카메라 위치에서 원근 투영(Perspective)으로 형성되는 사다리꼴 절두체.
가까운 곳은 좁고 먼 곳은 넓어진다.

      ╱‾‾‾‾‾‾‾‾‾╲
     ╱           ╲
    ╱    카메라    ╲
   ╱    시야 영역    ╲
   ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 Near              Far
```

입력: `camera.GetViewMatrix() × camera.GetProjectionMatrix()`

### 5.2 섀도우 캐스케이드 프러스텀

```
각 캐스케이드는 직교 투영(Orthographic)으로 형성되는 직육면체.
빛 방향을 기준으로 공간을 분할한다.

   ┌──────┐  ┌──────────┐  ┌────────────────┐
   │ C0   │  │   C1     │  │      C2        │
   │ 가까움│  │  중간    │  │     먼 곳       │
   └──────┘  └──────────┘  └────────────────┘
```

입력: `light.GetViewMatrix() × light.GetProjectionMatrixFromCascade(i)` (i = 0, 1, 2)

3개 캐스케이드 중 하나라도 통과하면 섀도우 리스트에 추가한다. 캐스케이드 간 중복 방지를 위해 첫 번째 통과 시 `break`한다.

### 5.3 미러 프러스텀

```
수면(Y=waterLevel)을 기준으로 물체를 수면에 반사한 영역
View Frustum은 일반적인 경우와 동일하지만, 물체의 `position`을 Reflection Matrix에 곱해서 FrustumCulling 연산을 진행한다.

          수면
    ──────────────────
    ╲     반사 영역    ╱
     ╲               ╱
      ╲             ╱
       ╲___________╱
```

미러 컬링은 카메라 프러스텀을 그대로 사용하되, 청크의 위치를 `mirrorPlaneMatrix`로 반사 변환한 후 테스트한다. 또한 AABB의 Y 확장 방향을 반전(`y *= -1`)하여, 반사 공간에서의 바운딩 박스를 올바르게 구성한다.

## 6. 회고

- 현재는 역변환 방식을 채용하지만, Gribb-Hartmann Culling을 활용하면 평면에 대한 행렬곱 없이 벡터만을 추출하여 더해 ClipSpace에 대한 평면을 구할 수 있다.
- AABB 8개 꼭짓점을 모두 테스트하는 대신, 평면 법선에 대한 p-vertex/n-vertex 기법을 사용하면 평면당 2번의 내적만으로 판정할 수 있어 꼭짓점 검사 횟수를 줄일 수 있다.

## 7. 나아가

### 평면의 방정식

```cpp
// 평면의 노멀
N = (a, b, c) // 평면의 노멀

// 평면의 한점
Q = (x1, y1, z1)

// 평면의 임의의 점
P = (x, y, z)

// 수직
N * (P - Q) = 0

// 수식
N*P - N*Q = 0
ax + by + cz - (ax1 + by1 + cz1) = 0
ax + by + cz + d = 0 (d = -N*Q)

// 평면 위의 점인가 판단
// 값 대입
 0  : 평면 위의 점
음수: 노멀벡터 반대 방향 (P-Q 벡터가 노멀벡터 반대 방향이라 내적 값이 음수임)
양수: 노멀벡터 방향 (P-Q 벡터가 노멀벡터 방향이라 내적 값이 양수임)
```

---

### AABB

- **Axis-Aligned Bounding Box**

#### AABB에서의 두 점과 n/p-vertex로 최적화

- AABB의 좌표로 최대최소점을 구하고 그것을 평면의 노멀벡터의 음양부호에 따라 n-vertex / p-vertex를 구할 수 있음
  - **n-vertex** : 평면 노멀에 가장 안쪽
  - **p-vertex** : 평면에 가장 바깥쪽
- n-vertex만을 가지고 충분히 프러스텀의 내외부 판단을 할 수 있음

```cpp
minPos = (0, 0, 0)
maxPos = minPos + (ChunkSize, ChunkSize, ChunkSize)

// 평면의 노멀벡터
N = (a, b, c)

// n vertex
nVertex.x = (a > 0) ? minPos.x : maxPos.x
nVertex.y = (b > 0) ? minPos.y : maxPos.y
nVertex.z = (c > 0) ? minPos.z : maxPos.z

// p vertex
pVertex.x = (a > 0) ? maxPos.x : minPos.x
pVertex.y = (b > 0) ? maxPos.y : minPos.y
pVertex.z = (c > 0) ? maxPos.z : minPos.z

// frustum culling
for (int i = 0; i < vfPlanes.size(); ++i)
{
    if (XMVectorGetX(XMPlaneDotCoord(vfPlanes[i], nVertex)) <= 0) continue;
    ...
}
```

---

### Gribb-Hartmann Culling

- Clip Space 부호를 활용한 방식
  - 결국 메쉬의 Position이 NDC 좌표에 들어와야하고, NDC 좌표 이전 ClipSpace에 대한 부호 검사를 진행

```cpp
// NDC
-1 <= x <= 1
-1 <= y <= 1
 0 <= z <= 1

// clip space
-w <= x <= w
-w <= y <= w
 0 <= z <= w

// P * ViewProj
    |                     |
P * | col0 col1 col2 col3 | => [P*col0, P*col1, P*col2, P*col3] => [x_c, y_c, z_c, w_c]
    |                     |

// 부호 판단
-w_c <= x_c <= w_c
-w_c <= y_c <= w_c
  0  <= z_c <= w_c

x_c + w_c >= 0 // left
w_c - x_c >= 0 // right
y_c + w_c >= 0 // bottom
w_c - y_c >= 0 // top
      z_c >= 0 // near
w_c - z_c >= 0 // far

// left 예시
x_c + w_c >= 0
P*col0 + P*col3 >= 0
P*(col0 + col3) >= 0
// col0 + col3 이 left의 평면 계수가 됨
```

---

### Gribb-Hartmann Culling + AABB 최적화

```cpp
// VP에서 column 추출 (row-major 기준)
Vector4 col0, col1, col2, col3;  // VP의 각 열

// Gribb-Hartmann으로 6개 평면 계수 구성
Vector4 planes[6] = {
    col0 + col3,   // Left
    col3 - col0,   // Right
    col1 + col3,   // Bottom
    col3 - col1,   // Top
    col2,          // Near
    col3 - col2,   // Far
};

for (int i = 0; i < 6; ++i) {
    float a = planes[i].x;
    float b = planes[i].y;
    float c = planes[i].z;
    float d = planes[i].w;

    // n-vertex 선택
    Vector3 n;
    n.x = (a > 0) ? min.x : max.x;
    n.y = (b > 0) ? min.y : max.y;
    n.z = (c > 0) ? min.z : max.z;

    // XMPlaneDotCoord 없이 직접 대입
    if (a*n.x + b*n.y + c*n.z + d < 0)
        return false;  // 컬링
}
return true;
```

---

### Frustum Viewer

VS에서 Camera.ConstantBuffer에 대한 View를 적절히 바꿔줌

```cpp
// Camera
// Debug Camera for Frustum Culling
m_cullingViewerOffsetPos = Vector3(0.0f, 128.0f, -128.0f);
m_cullingViewerPos = m_eyePos + m_cullingViewerOffsetPos;

Quaternion qPitch = Quaternion(
    Vector3(1.0f, 0.0f, 0.0f) * sinf(XM_PIDIV2 * 0.25f), cosf(XM_PIDIV2 * 0.25f));
m_cullingViewerForward = Vector3::Transform(Vector3(0.0f, 0.0f, 1.0f), Matrix::CreateFromQuaternion(qPitch));
m_cullingViewerUp = Vector3::Transform(Vector3(0.0f, 1.0f, 0.0f), Matrix::CreateFromQuaternion(qPitch));

m_constantData.view = XMMatrixLookToLH(m_cullingViewerPos, m_cullingViewerForward, m_cullingViewerUp);
m_constantData.view = m_constantData.view.Transpose();

if (!DXUtils::CreateConstantBuffer(m_cullingViewerConstantBuffer, m_constantData)) {
    std::cout << "failed create debug camera constant buffer" << std::endl;
    return false;
}

// App::RenderFrustumCullingViewer()
Graphics::context->VSSetConstantBuffers(
		8, 1, m_camera.m_cullingViewerConstantBuffer.GetAddressOf());
```

Chunk Render는 단순히 LOD Basic으로 렌더링 -> SRV 그대로 사용

```cpp
std::vector<ID3D11ShaderResourceView*> ppSRVs;
ppSRVs.push_back(Graphics::blockAtlasMapSRV.Get());
ppSRVs.push_back(Graphics::normalAtlasMapSRV.Get());
ppSRVs.push_back(Graphics::merAtlasMapSRV.Get());
ppSRVs.push_back(Graphics::grassColorMapSRV.Get());
ppSRVs.push_back(Graphics::foliageColorMapSRV.Get());
ppSRVs.push_back(Graphics::climateMapSRV.Get());
Graphics::context->PSSetShaderResources(0, (UINT)ppSRVs.size(), ppSRVs.data());

ChunkManager::GetInstance()->RenderBasicAlbedo();
```

ViewFrustum을 NDC로 메쉬를 만들고 Camera의 Inv(ViewProj)을 VS에서 변환하여 WorldPosition으로 역변환하여 사용
그것을 현재 Viewer 카메라에 맞게 View, Proj 변환

```hlsl
vsOutput main(vsInput input)
{
    vsOutput output;

    float4 frustumNDCPosition = float4(input.position, 1.0);

    float4 frustumViewPosition = mul(frustumNDCPosition, invProj);
    frustumViewPosition.xyz /= frustumViewPosition.w;

    float4 frustumWorldPosition = mul(frustumViewPosition, invView);

    output.posProj = mul(frustumWorldPosition, view);
    output.posProj = mul(output.posProj, proj);

    output.color = float3(1.0, 0.0, 0.0);

    return output;
}
```
