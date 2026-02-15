# Chunk Update Pipeline

## 1. 개요

ChunkManager::Update()는 매 프레임 호출되는 청크 관리의 핵심 함수로, 청크의 생성부터 소멸까지 전체 생명주기를 8단계 파이프라인으로 관리한다. 카메라 이동에 따른 청크 목록 갱신, 비동기 로딩/패치, 프러스텀 컬링 기반 렌더 리스트 구성, 인스턴스 수집, 등장 애니메이션까지 한 프레임 안에서 순차적으로 처리한다.

## 2. 도입 동기

복셀 월드에서는 수백 개의 청크가 동시에 존재하며, 각 청크는 로딩, 패치, 언로딩이라는 서로 다른 상태를 가진다. 이를 단순히 순차 처리하면 프레임 드롭이 발생하고, 무질서하게 비동기 처리하면 의존 관계가 깨진다. 각 단계를 명확히 분리한 파이프라인 구조를 도입하여 프레임당 작업량을 제어하면서도 비동기 처리의 이점을 유지할 필요가 있었다.

## 3. Update 파이프라인 전체 흐름

```
ChunkManager::Update(dt, camera, light, mouseLeft, mouseRight)
│
├─ [1] UpdateChunkList          카메라 이동 시 청크 목록 갱신
├─ [2] Block Picking            마우스 입력 → 블록 추가/제거 패치 등록
├─ [3] UpdateLoadChunkList      비동기 청크 로딩 (Init 스레드)
├─ [4] UpdateUnloadChunkList    범위 밖 청크 해제
├─ [5] UpdatePatchChunkMap      비동기 청크 패치 (Patch 스레드)
├─ [6] UpdateRenderChunkList    프러스텀 컬링 → 렌더 리스트 구성
├─ [7] UpdateInstanceInfoList   인스턴스(풀, 꽃) 정보 수집
└─ [8] UpdateChunkConstant      등장 애니메이션 + Constant Buffer 갱신
```

## 4. 파이프라인 단계별 구현

### 4.1 UpdateChunkList - 청크 목록 갱신

카메라가 청크 경계를 넘을 때만 실행된다. `camera.m_isOnChunkDirtyFlag`가 트리거 역할을 한다.

```
카메라 청크 좌표 기준으로 CHUNK_COUNT x CHUNK_COUNT x MAX_HEIGHT_CHUNK_COUNT 범위 순회
├─ m_chunkMap에 없는 위치 → Object Pool에서 Chunk 할당 → m_loadChunkList에 추가
└─ 새 범위에 포함되지 않는 기존 청크 → m_unloadChunkList에 추가
```

- 청크는 `GetChunkFromPool()`로 풀에서 가져오며, 풀이 비면 해당 위치는 로딩을 건너뛴다.
- 수평 범위는 `CHUNK_COUNT = 2 * (MAX_RENDER_DISTANCE / CHUNK_SIZE) + 1`로 결정된다.
- 수직 범위는 `MAX_HEIGHT_CHUNK_COUNT = 8`로 고정 (256 / 32).

### 4.2 Block Picking - 블록 추가/제거

카메라의 피킹 오브젝트가 있을 때 마우스 입력을 처리한다.

| 입력 | 동작 | 블록 타입 |
|------|------|----------|
| 좌클릭 | `RemoveBlockPatchAt` | AIR (수면 아래면 WATER) |
| 우클릭 | `AddBlockPatchAt` | GOLD (face 방향 오프셋 적용) |

두 경우 모두 다음 과정을 거친다:
1. 월드 좌표 → 청크 오프셋 좌표 + 로컬 좌표 분리
2. `PatchData` 생성 → `m_cameraPatchChunkMap`과 `m_patchChunkMap`에 등록
3. `PropagatePatchByEdgeBlock()`: 로컬 좌표가 0 또는 31(청크 경계)이면 인접 청크에도 패치 전파

`m_cameraPatchChunkMap`은 플레이어 입력으로 생긴 패치를 영구적으로 보관하여, 해당 청크가 언로드 후 재로딩될 때도 패치를 다시 적용할 수 있게 한다.

### 4.3 UpdateLoadChunkList - 비동기 청크 로딩

```
m_loadChunkList (카메라 거리순 정렬, 가까운 청크 우선)
│
├─ 가까운 순서대로 pop → std::async(Chunk::Initialize) 디스패치
│   (최대 m_initThreadCount 개 동시 실행)
│
└─ 완료된 future 처리:
    ├─ Dependency Map 등록 (나무 등 청크 경계 오브젝트)
    ├─ 역방향 Lookup: 이미 로드된 청크가 나를 향한 패치를 갖고 있는지 확인
    ├─ cameraPatchChunkMap 병합 (플레이어 패치 적용)
    ├─ UpdateCpuBufferCount() → UpdateChunkBuffer() (GPU 업로드)
    ├─ SetLoad(true), SetUpdateRequired(true)
    └─ ChunkLoadMemory 반환 → 메모리 풀로 복귀
```

**핵심 자료구조 (Dependency 관리):**

| 자료구조 | 타입 | 역할 |
|----------|------|------|
| `m_patchDependencyMap` | `Map<source, Map<target, PatchDataSet>>` | source 청크가 생성한 target 향 패치 데이터 보관 |
| `m_lookupDependencySet` | `Map<target, Set<source>>` | target 청크를 향하는 source 목록 (역방향 조회) |
| `m_patchedChunkSet` | `Map<target, Set<source>>` | target에 이미 적용된 source 패치 추적 (중복 방지) |

초기화 완료 시 두 방향의 의존성을 모두 처리한다:
- **정방향**: 내가 생성한 패치 데이터를 이미 로드된 인접 청크에 전달
- **역방향**: 이미 로드된 인접 청크가 나를 위해 생성해둔 패치 데이터를 가져옴

### 4.4 UpdateUnloadChunkList - 청크 해제

범위 밖으로 벗어난 청크를 정리한다.

```
m_unloadChunkList 순회:
├─ m_chunkMap에서 제거
├─ m_patchChunkMap에서 제거
├─ m_patchDependencyMap에서 제거 + lookupDependencySet 역참조 정리
├─ m_patchedChunkSet에서 제거
├─ chunk->Clear()
└─ ReleaseChunkToPool() → Object Pool로 반환
```

언로딩 시 해당 청크가 다른 청크에 남긴 의존 관계를 `m_lookupDependencySet`을 통해 역추적하여 깨끗하게 제거한다. 이를 통해 의존성 맵의 메모리 누수를 방지한다.

### 4.5 UpdatePatchChunkMap - 비동기 청크 패치

블록 변경이나 나무 전파 등으로 메시를 재생성해야 하는 청크를 처리한다.

```
m_patchChunkMap의 키(청크 위치) 수집 → 카메라 거리순 정렬 (가까운 우선)
│
├─ 유효성 검사: 청크 존재 여부, 로드 완료 여부, 패치 진행중 여부
├─ std::async(Chunk::Patch) 디스패치 (최대 m_patchThreadCount 개)
│
└─ 완료된 future 처리:
    ├─ UpdateCpuBufferCount()
    ├─ onPatchDirtyFlag == true 일 때만 GPU 버퍼 갱신
    ├─ SetIsPatching(false)
    └─ ChunkLoadMemory 반환
```

`Chunk::Patch()`는 PatchDataSet을 순회하며:
- **Instance 패치**: 해당 위치가 투명 블록이면 인스턴스 교체
- **Block 패치**: 블록 타입이 변경되었으면 교체 + `m_onPatchDirtyFlag = true`
- Dirty flag가 설정되면 `InitWorldVerticesData()`로 메시 전체를 재생성

### 4.6 UpdateRenderChunkList - 렌더 리스트 구성

모든 로드된 청크를 순회하며 프러스텀 컬링으로 3개의 렌더 리스트를 구성한다.

| 리스트 | 컬링 기준 | 용도 |
|--------|----------|------|
| `m_renderChunkList` | 카메라 프러스텀 | 메인 렌더링 (Opaque + SemiAlpha + Transparency) |
| `m_renderShadowChunkList` | 3개 캐스케이드 프러스텀 중 하나라도 통과 | 섀도우 맵 렌더링 |
| `m_renderMirrorChunkList` | 미러 평면 반사 프러스텀 | 수면 반사 렌더링 |

프러스텀 컬링은 NDC 8개 꼭짓점을 월드 공간으로 역변환한 뒤 6개 평면을 구성하고, 청크의 AABB 8개 꼭짓점이 모든 평면 밖에 있으면 제외하는 방식이다.

### 4.7 UpdateInstanceInfoList - 인스턴스 정보 수집

풀, 꽃, 덩굴 등 인스턴스 렌더링 데이터를 수집한다.

```
m_renderChunkList 순회:
├─ 아직 애니메이션 중인 청크(IsUpdateRequired) → 건너뜀
├─ LOD_RENDER_DISTANCE 초과 → 건너뜀
│
└─ 청크의 instanceMap 순회:
    ├─ faceFlag > 0: AddInstanceInfoBySplitFace() (덩굴 - 방향별 분리)
    └─ faceFlag == 0: AddInstanceInfo() (풀, 꽃 - 회전/오프셋 적용)
```

수집 완료 후 셰이프 타입별(CROSS, FENCE, SQUARE, FLOOR)로 GPU 버퍼를 `ResizeBuffer()` + `UpdateBuffer()`로 갱신한다.

### 4.8 UpdateChunkConstant - 등장 애니메이션

새로 로드된 청크에 아래에서 위로 올라오는 등장 애니메이션을 적용한다.

```cpp
// Chunk::Initialize에서 시작 위치 설정
m_position = Vector3(offsetX, -2.0f * CHUNK_SIZE, offsetZ);  // 청크 크기 2배 아래

// Chunk::Update에서 매 프레임 보간
m_position.y += 50.0f * dt;
if (m_position.y > m_offsetPosition.y)
    m_position.y = m_offsetPosition.y;  // 도착하면 고정
```

- `m_position`이 `m_offsetPosition`에 도달하면 `SetUpdateRequired(false)`로 애니메이션 종료
- Constant Buffer에 world 행렬(Translation)을 업데이트하여 GPU에 전달

## 5. 스레드 모델

```
메인 스레드 ─────────────────────────────────────────────────
  │  Update() 호출, future 완료 확인, GPU 버퍼 업로드
  │
  ├─ Init Thread Pool (1~3개)
  │   └─ Chunk::Initialize() 실행 (지형 생성 + 메시 생성)
  │
  └─ Patch Thread Pool (1~2개)
      └─ Chunk::Patch() 실행 (블록 변경 + 메시 재생성)
```

- 전체 사용 가능 스레드: `min(6, hardware_concurrency) - 1`
- Init/Patch 스레드 수는 `std::clamp`로 범위 제한
- `ChunkLoadMemory`를 풀링하여 스레드 간 메모리 할당 경합 방지
- GPU 버퍼 업로드는 반드시 메인 스레드에서 수행 (DX11 제약)
- `UpdateCpuBufferCount()`로 멀티스레드 환경에서 안전하게 버퍼 크기를 스냅샷

## 6. Object Pool과 메모리 관리

| 풀 | 크기 | 역할 |
|----|------|------|
| `m_chunkPool` | `CHUNK_POOL_SIZE` (= CHUNK_COUNT_P^2 * MAX_HEIGHT_CHUNK_COUNT_P) | Chunk 객체 재사용 |
| `m_chunkLoadMemoryPool` | initThreadCount + patchThreadCount | 스레드별 작업 메모리 재사용 |

- Chunk Pool이 고갈되면 새 청크 로딩을 건너뛴다 (`GetChunkFromPool() == nullptr`)
- GPU 버퍼는 풀 크기만큼 미리 벡터를 할당해두고 청크 ID로 인덱싱한다
- 인스턴스 버퍼는 `MAX_INSTANCE_BUFFER_SIZE(8MB)` 제한 내에서 동적으로 리사이즈

## 7. 회고

- 파이프라인 구조 덕분에 각 단계를 독립적으로 최적화할 수 있다. 예를 들어 로딩과 패치의 스레드 수를 별도로 조절하여 상황에 맞게 리소스를 배분할 수 있다.
- 의존성 관리를 위한 자료구조(patchDependencyMap, lookupDependencySet, patchedChunkSet)가 다소 복잡하지만, 나무 등 청크 경계를 넘는 오브젝트를 정확히 처리하기 위해 불가피한 구조다.
- 현재 패치 시 `InitWorldVerticesData()`로 메시 전체를 재생성하는데, 변경된 블록 주변만 부분 재생성하는 방식으로 개선하면 패치 성능을 높일 수 있다.
- 프러스텀 컬링을 매 프레임 모든 청크에 대해 수행하는데, 공간 분할(Octree 등)을 도입하면 컬링 비용을 줄일 수 있다.
