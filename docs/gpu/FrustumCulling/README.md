# Frustum Culling

## 1. 개요

Frustum Culling은 카메라의 시야 절두체(frustum) 밖에 있는 청크를 렌더링 대상에서 제외하는 기법이다. 매 프레임 로드된 모든 청크를 대상으로 3종류의 프러스텀 검사를 수행하여, 카메라 렌더 리스트 / 섀도우 렌더 리스트 / 미러 렌더 리스트를 각각 구성한다. 화면에 보이지 않는 청크의 드로우 콜을 제거하여 GPU 부하를 줄이는 핵심 최적화다.

## 2. 도입 동기

복셀 월드에서 렌더 거리 내 로드된 청크 수는 수백 개에 달한다. 이 중 카메라 시야에 실제로 들어오는 청크는 일부에 불과하지만, 컬링 없이 모두 GPU에 보내면 불필요한 버텍스 처리와 래스터라이징이 발생한다. 프러스텀 컬링으로 보이지 않는 청크를 CPU 단계에서 걸러내면, GPU에 전달되는 드로우 콜 수를 대폭 줄일 수 있다.

추가로 섀도우 맵과 수면 반사도 각각 별도의 프러스텀을 가지므로, 이들에 대해서도 독립적인 컬링이 필요하다.

## 3. 핵심 아이디어

### 3.1 NDC 역변환 기반 프러스텀 구성

일반적인 프러스텀 컬링은 뷰-프로젝션 행렬에서 6개 평면 방정식을 직접 추출하는 방식을 사용한다. 이 프로젝트에서는 다른 접근을 택했다.

```
NDC 큐브의 8개 꼭짓점 → (View × Projection)⁻¹ 역변환 → 월드 공간 8개 꼭짓점 → 6개 평면 구성
```

NDC 공간의 단위 큐브 꼭짓점을 월드 공간으로 역변환하면 프러스텀의 실제 월드 좌표를 얻고, 이 꼭짓점들로부터 6개 평면을 구성하는 방식이다. 이 방식은 카메라 프러스텀뿐 아니라 캐스케이드 섀도우의 직교 프러스텀에도 동일한 코드로 적용할 수 있다는 장점이 있다.

### 3.2 AABB 보수적 판정

청크는 32x32x32 크기의 축 정렬 바운딩 박스(AABB)로 근사된다. AABB의 8개 꼭짓점 모두가 어느 한 평면의 바깥에 있어야만 해당 청크를 제외(reject)한다. 꼭짓점 하나라도 안쪽에 있으면 보수적으로 통과(accept)시킨다. 이는 거짓 음성(보이는데 제외)을 방지하는 대신 거짓 양성(안 보이는데 통과)을 허용하는 전략이다.

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

| 렌더 리스트 | 프러스텀 소스 | 용도 |
|-------------|--------------|------|
| `m_renderChunkList` | 카메라 View × Projection | 메인 렌더링 (Opaque + SemiAlpha + Transparency) |
| `m_renderShadowChunkList` | Light View × Cascade Projection[0~2] | 3개 캐스케이드 섀도우 맵 |
| `m_renderMirrorChunkList` | 카메라 프러스텀 + 미러 평면 반사 | 수면 반사 렌더링 |

섀도우 컬링에서 3개 캐스케이드 중 **하나라도** 통과하면 섀도우 리스트에 추가하고 `break`하는 이유는, Geometry Shader가 하나의 청크를 3개 캐스케이드에 동시에 렌더링하므로, 어느 캐스케이드든 필요하면 해당 청크 전체를 보내야 하기 때문이다.

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

| 프러스텀 | View 행렬 | Projection 행렬 |
|----------|-----------|-----------------|
| 카메라 / 미러 | Camera View | Camera Perspective Projection |
| 섀도우 캐스케이드 i | Light View | Cascade Orthographic Projection[i] |

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
planes[0] = PlaneFromPoints(worldPos[0], worldPos[1], worldPos[2])  // Near
planes[1] = PlaneFromPoints(worldPos[7], worldPos[6], worldPos[5])  // Far
planes[2] = PlaneFromPoints(worldPos[4], worldPos[5], worldPos[1])  // Top
planes[3] = PlaneFromPoints(worldPos[3], worldPos[2], worldPos[6])  // Bottom
planes[4] = PlaneFromPoints(worldPos[4], worldPos[0], worldPos[3])  // Left
planes[5] = PlaneFromPoints(worldPos[1], worldPos[5], worldPos[6])  // Right
```

```
프러스텀 전개도:

         4───────────5
        /│          /│
       / │   Far   / │
      7───────────6  │
      │  │  Top   │  │
      │  0───────────1     Near (Z=0): 0,1,2,3
      │ /   Near  │ /      Far  (Z=1): 4,5,6,7
      │/  Bottom  │/
      3───────────2
```

`XMPlaneFromPoints()`는 세 점으로 평면의 법선과 거리(Ax+By+Cz+D=0)를 계산한다. 점의 순서(와인딩)에 따라 법선 방향이 결정되며, 프러스텀 안쪽을 향하도록 구성된다.

### 4.3 AABB vs 프러스텀 판정

```cpp
float x = (float)Chunk::CHUNK_SIZE;  // 32
float y = (float)Chunk::CHUNK_SIZE;  // 32
float z = (float)Chunk::CHUNK_SIZE;  // 32
if (useMirror)
    y *= -1;  // 미러 반사 시 Y축 반전
```

청크의 AABB는 `position`을 원점으로 `(position + 32, position + 32, position + 32)`까지의 축 정렬 박스다. 미러 렌더링 시에는 Y축이 반전되어 `position.y - 32` 방향으로 확장된다.

```cpp
for (int i = 0; i < 6; ++i) {
    if (PlaneDotCoord(planes[i], position)                         < 0) continue;
    if (PlaneDotCoord(planes[i], position + (x, 0, 0))           <= 0) continue;
    if (PlaneDotCoord(planes[i], position + (0, y, 0))           <= 0) continue;
    if (PlaneDotCoord(planes[i], position + (x, y, 0))           <= 0) continue;
    if (PlaneDotCoord(planes[i], position + (0, 0, z))           <= 0) continue;
    if (PlaneDotCoord(planes[i], position + (x, 0, z))           <= 0) continue;
    if (PlaneDotCoord(planes[i], position + (0, y, z))           <= 0) continue;
    if (PlaneDotCoord(planes[i], position + (x, y, z))           <= 0) continue;
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

**보수적 판정의 의미:**

```
[정확히 컬링]           [거짓 양성 - 통과시킴]     [절대 없음]
 AABB 전체가             AABB가 프러스텀 모서리      보이는 청크를
 프러스텀 밖              근처에서 교차하지만         놓치는 경우
                         실제로는 밖
```

거짓 양성은 프러스텀 모서리 근처에서 발생할 수 있다. 이는 불필요한 드로우 콜이 소수 추가되는 것이므로, 보이는 청크를 놓치는 거짓 음성보다 훨씬 안전하다.

## 5. 3가지 프러스텀 비교

### 5.1 카메라 프러스텀

```
카메라 위치에서 원근 투영(Perspective)으로 형성되는 사다리꼴 절두체.
가까운 곳은 좁고 먼 곳은 넓어진다.

      ╱‾‾‾‾‾‾‾‾‾╲
     ╱             ╲
    ╱    카메라      ╲
   ╱    시야 영역     ╲
   ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
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
수면(Y=waterLevel)을 기준으로 카메라 프러스텀을 Y축 반사한 영역.
반사된 세계를 렌더링할 때 사용한다.

          수면
    ──────────────────
    ╲     반사 영역    ╱
     ╲               ╱
      ╲             ╱
       ╲___________╱
```

미러 컬링은 카메라 프러스텀을 그대로 사용하되, 청크의 위치를 `mirrorPlaneMatrix`로 반사 변환한 후 테스트한다. 또한 AABB의 Y 확장 방향을 반전(`y *= -1`)하여, 반사 공간에서의 바운딩 박스를 올바르게 구성한다.

## 6. 회고

- 현재 매 프레임 모든 청크에 대해 프러스텀 평면을 재구성하고 있다. 프러스텀 평면은 카메라/라이트가 변경될 때만 바뀌므로, 프레임 시작 시 한 번만 계산하여 캐싱하면 역행렬 연산과 평면 구성을 중복 수행하지 않을 수 있다.
- AABB 8개 꼭짓점을 모두 테스트하는 대신, 평면 법선에 대한 p-vertex/n-vertex 기법을 사용하면 평면당 2번의 내적만으로 판정할 수 있어 꼭짓점 검사 횟수를 줄일 수 있다.
- 공간 분할 구조(Octree, BVH 등)를 도입하면 프러스텀 밖의 청크 그룹을 한 번에 제외하여, 개별 청크 검사 횟수를 줄일 수 있다.
