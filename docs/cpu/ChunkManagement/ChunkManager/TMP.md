# ChunkManager

## 1. 개요

`ChunkManager`는 Voxen 월드의 모든 청크를 중앙에서 지휘하는 관리자다. Chunk가 자기 자신의 CPU 데이터만 소유하는 순수한 데이터 홀더라면([ChunkStructure](../ChunkStructure/README.md) 참조), ChunkManager는 그 위에서 다음 책임을 진다.

- 청크의 라이프사이클: 어떤 청크를 언제 로드/언로드/패치할지 결정
- GPU 리소스 관리: 모든 청크의 버텍스/인덱스/상수 버퍼, 인스턴스 버퍼 소유
- 프레임 파이프라인: 매 프레임 위 작업을 순서대로 실행
- 렌더 리스트 구성: 프러스텀 컬링을 통해 이번 프레임에 그릴 청크를 골라냄
- 렌더링 실행: 실제 D3D11 draw call 발행

Chunk가 "무엇을 그릴지"의 데이터라면, ChunkManager는 **"언제, 어떻게, 얼마나"의 실행 계층**이다.

## 2. Singleton

ChunkManager는 프로세스 전체에서 유일한 인스턴스다.

```cpp
ChunkManager* ChunkManager::GetInstance()
{
    static ChunkManager* chunkManager = nullptr;
    if (chunkManager == nullptr)
        chunkManager = new ChunkManager();
    return chunkManager;
}
```

싱글턴을 채택한 이유는 여러 시스템에서 동일한 청크 상태를 참조·조작해야 하기 때문이다.

- `App` — 매 프레임 `Update()` 호출
- `Camera` — 카메라 위치 기반 청크 목록 갱신 트리거, DDA 피킹 시 블록 조회
- 렌더 파이프라인 — 렌더 패스마다 `RenderBasic()`, `RenderMirrorWorld()`, `RenderBasicShadowMap()` 등 호출
- 블록 피킹 로직 — `AddBlockPatchAt`, `RemoveBlockPatchAt`으로 월드 상태 변경

이 호출 지점이 모두 청크의 최신 상태를 공유해야 하므로 인스턴스가 여러 개일 수 없다. 생성자·복사 대입 연산자는 `private`이며, 소멸까지의 상태 일관성을 명시적으로 보장한다.

## 3. 미리 할당된 풀 (Pre-allocated Pools)

ChunkManager가 매니저다운 역할을 할 수 있는 핵심 근거는 **모든 재사용 대상 리소스를 시작 시점에 한꺼번에 할당해 둔다**는 것이다. 런타임 중 `new`/`delete`, `CreateBuffer` 같은 비용 큰 호출을 최대한 피한다.

### 3.1 Chunk Object Pool

```cpp
std::vector<Chunk*> m_chunkPool;   // 초기 크기 CHUNK_POOL_SIZE

void InitChunkPool() {
    for (int id = 0; id < CHUNK_POOL_SIZE; ++id) {
        Chunk* chunk = new Chunk(id);   // ID를 부여하며 생성
        chunk->Clear();
        m_chunkPool.push_back(chunk);
    }
}
```

카메라 이동에 따라 청크는 끊임없이 로드-언로드를 반복한다. 그때마다 `new Chunk`를 하면 매번 `m_blocks[34][34][34]` (약 39KB) + 여러 vector가 힙에 할당된다. 대신 프로그램 시작 시 `CHUNK_POOL_SIZE` 개(현재 `34 × 34 × 10 = 11,560`)의 Chunk를 미리 할당해 두고, 필요할 때 `GetChunkFromPool()`로 꺼내 쓰고 `ReleaseChunkToPool()`로 되돌린다.

각 Chunk에 부여된 `m_id`는 생성 시점부터 소멸까지 **불변**이며, 아래 GPU 버퍼 배열의 인덱스로 그대로 재사용된다.

### 3.2 ChunkLoadMemory Pool

```cpp
std::vector<ChunkLoadMemory*> m_chunkLoadMemoryPool;   // initThreadCount + patchThreadCount 개

void InitChunkLoadMemoryPool() {
    for (unsigned int i = 0; i < m_initThreadCount + m_patchThreadCount; ++i)
        m_chunkLoadMemoryPool.push_back(new ChunkLoadMemory());
}
```

`ChunkLoadMemory`는 워커 스레드가 청크 초기화·패치 시 필요한 대용량 임시 버퍼(노이즈 배열 7종, 컬럼 비트 배열 4종, 바이옴 맵 등, 도합 수백 KB)를 담는 컨테이너다. 청크 하나 로드할 때마다 이걸 스택에 잡거나 힙에 새로 만들면 스레드 스택 오버플로우 또는 할당 병목이 발생한다.

동시에 실행될 수 있는 워커 수(`m_initThreadCount + m_patchThreadCount`)만큼만 있으면 충분하므로, 그만큼만 미리 할당하고 워커가 대여·반환하는 방식이 자연스럽다.

풀 대여·반환은 사용자 지시대로 RAII에 가까운 규칙을 따른다: `GetChunkLoadMemoryFromPool()`로 대여, 작업 종료 후 `ReleaseChunkLoadMemoryToPool()`로 반환하며, 반환 직전 `Clear()`로 상태를 초기화한다.

### 3.3 GPU 버퍼 배열

```cpp
std::vector<ComPtr<ID3D11Buffer>> m_opaqueVertexBuffers;       // 크기 CHUNK_POOL_SIZE
std::vector<ComPtr<ID3D11Buffer>> m_opaqueIndexBuffers;
std::vector<ComPtr<ID3D11Buffer>> m_transparencyVertexBuffers;
// ... (LowLod, SemiAlpha, Constant 각각 존재)

std::vector<ComPtr<ID3D11Buffer>> m_instanceVertexBuffers;   // 크기 INSTANCE_SHAPE_COUNT (=4)
std::vector<ComPtr<ID3D11Buffer>> m_instanceIndexBuffers;
std::vector<ComPtr<ID3D11Buffer>> m_instanceInfoBuffers;
```

GPU 버퍼도 마찬가지로 시작 시점에 벡터 크기만 확보한다(내부 `ComPtr`는 초기엔 `nullptr`). 청크가 처음 로드될 때 해당 슬롯의 버퍼가 실제로 생성되고, 이후 언로드-재로드가 반복되어도 같은 슬롯을 계속 재사용한다. 자세한 재사용 전략은 [§5](#5-gpu-버퍼-재사용-전략-createupdate-분리)에서 다룬다.

### 3.4 왜 이렇게까지 미리 할당하는가

복셀 월드의 청크 로드는 카메라가 한 청크 경계를 넘을 때마다 수십~수백 개가 한꺼번에 발생한다. 이때 아래 세 종류의 힙 할당·해제가 모두 발생하면 프레임 스파이크로 이어진다.

1. Chunk 객체(약 39KB 배열 포함)
2. ChunkLoadMemory (수백 KB 임시 버퍼)
3. D3D11 리소스 (드라이버 커널 진입 포함)

이 셋을 모두 풀링해 두면, **로드 중 발생하는 유일한 할당은 CPU 메시 vector의 push_back**뿐이다. 그것도 vector의 `capacity`는 유지된 채로 재사용되므로 실질적으로는 초회 로드에만 발생한다.

## 4. GPU 버퍼가 왜 Chunk가 아닌 ChunkManager에 있는가

Chunk는 CPU 메시 데이터만, GPU 버퍼는 ChunkManager가 소유한다. 자연스러운 첫 설계("각 Chunk가 자기 GPU 버퍼를 소유")를 벗어난 결정이므로 근거를 명시한다. (같은 내용을 Chunk 쪽 시각에서 [ChunkStructure §5](../ChunkStructure/README.md)에서도 다룬다.)

**출발점은 Instance Rendering이었다.** 풀·꽃·덩굴 같은 오브젝트는 청크별로 그리지 않는다. 대신 형상(Cross/Fence/Square/Floor) **4종**으로 분류해 놓고, 매 프레임 렌더 대상 청크의 인스턴스 정보를 형상별로 모은 뒤 `DrawIndexedInstanced`로 한 번에 그린다.

```cpp
// 형상별 공유 메시 + 형상별 인스턴스 정보 버퍼
std::vector<ComPtr<ID3D11Buffer>> m_instanceVertexBuffers;   // [4]
std::vector<ComPtr<ID3D11Buffer>> m_instanceIndexBuffers;    // [4]
std::vector<ComPtr<ID3D11Buffer>> m_instanceInfoBuffers;     // [4]  (동적, 매 프레임 갱신)
std::vector<std::vector<InstanceInfoVertex>> m_instanceInfoList;   // [4]
```

즉 **인스턴스 GPU 버퍼는 청크의 소유물이 아니라 렌더 시스템의 공유 자원**이다. 청크 단위로 그리지 않으니 청크가 들고 있을 수도 없다. 자연히 ChunkManager로 올라왔다.

여기서 **이원 구조 문제**가 발생했다. 인스턴스 버퍼는 Manager, 블록 버퍼는 Chunk에 있게 되면서:

- GPU 리소스 리사이즈·업로드 코드가 두 곳으로 갈라짐
- DX11 디바이스/컨텍스트 접근 경로가 두 위치에서 필요
- 스레드 경계(워커는 CPU 데이터, 메인은 GPU 업로드)가 클래스별로 서로 다른 규칙을 갖게 됨

이를 정리하기 위해 **블록 관련 GPU 버퍼도 ChunkManager로 이동**했다. 그 결과 얻은 이점.

- DX11 Immediate Context 단일 스레드 제약을 자연스럽게 만족한다. 워커는 CPU 데이터만 채우고, GPU 업로드는 매니저가 메인 스레드에서 수행한다.
- Chunk 객체가 D3D11 리소스를 전혀 다루지 않게 되면서 Chunk의 책임 경계가 명확해졌다.
- `DXUtils::ResizeBuffer` / `UpdateBuffer` 같은 헬퍼가 블록/인스턴스에 통일된 방식으로 쓰인다.

## 5. GPU 버퍼 재사용 전략 (Create/Update 분리)

D3D11에서 `ID3D11Buffer`를 매번 `CreateBuffer`로 만들었다가 `Release`하는 것은 드라이버 커널 진입을 수반하는 비싼 연산이다. Voxen은 이를 다음 두 헬퍼로 우회한다.

- `DXUtils::ResizeBuffer(buffer, data, bindFlag)` — 버퍼가 `nullptr`이거나 기존 크기가 부족할 때만 **재생성**.
- `DXUtils::UpdateBuffer(buffer, data)` — `Map`/`Unmap`으로 기존 버퍼 메모리에 데이터만 **덮어쓰기**.

`UpdateChunkGPUBuffer()`는 청크의 4종 메시 각각에 대해 두 함수를 순서대로 호출한다.

```cpp
DXUtils::ResizeBuffer(m_opaqueVertexBuffers[id], chunk->GetOpaqueVertices(), D3D11_BIND_VERTEX_BUFFER);
DXUtils::ResizeBuffer(m_opaqueIndexBuffers[id],  chunk->GetOpaqueIndices(),  D3D11_BIND_INDEX_BUFFER);
DXUtils::UpdateBuffer(m_opaqueVertexBuffers[id], chunk->GetOpaqueVertices());
DXUtils::UpdateBuffer(m_opaqueIndexBuffers[id],  chunk->GetOpaqueIndices());
```

### 청크가 언로드되어도 GPU 버퍼는 살아있다

이 설계의 핵심은 **Unload 시 GPU 버퍼를 해제하지 않는다**는 것이다. Chunk는 Pool로 반환되면서 CPU vector만 `clear()`되고, 대응하는 GPU 버퍼(`m_opaqueVertexBuffers[id]` 등)는 그대로 남는다.

다음 청크가 같은 `m_id` 슬롯을 재활용할 때:

- 기존 버퍼 크기가 충분하면 → `UpdateBuffer` 한 번으로 끝난다 (커널 진입 없음, Map/Unmap만).
- 새 청크의 메시가 더 크면 → `ResizeBuffer`가 그때만 재생성한다.

첫 몇 초 동안 서로 다른 크기의 청크가 슬롯을 지나가면서 버퍼가 점차 "그 슬롯을 지나간 가장 큰 청크의 크기"로 안정화된다. 이후로는 대부분 `UpdateBuffer`만 발생한다.

### 인스턴스 정보 버퍼

인스턴스 정보 버퍼는 위와 성격이 조금 다르다. 매 프레임 렌더 대상 청크가 바뀌면서 형상별 인스턴스 개수가 요동친다.

- 시작 시점에 `MAX_INSTANCE_BUFFER_SIZE(8MB)` 크기의 dynamic 버퍼로 만들어 둔다 (`MakeInstanceInfoBuffer`).
- 매 프레임 `UpdateInstanceInfoList`에서 `ResizeBuffer`(현재 사이즈 + 1024 여유분) → `UpdateBuffer`로 갱신.
- 8MB 상한 내에서는 재생성이 거의 발생하지 않고 Map/Unmap만 반복된다.

### CPU 버퍼도 같은 원리

Chunk의 CPU 메시 vector도 언로드 시 `clear()`만 하고 `shrink_to_fit()`은 하지 않는다. `capacity`가 유지되므로 같은 슬롯 재사용 시 vector의 재할당도 회피된다.

## 6. Update - 프레임 파이프라인

ChunkManager의 심장부는 `Update(dt, camera, light)`다. 매 프레임 이 함수 하나가 청크 관리 전체를 굴린다.

```
Update(dt, camera, light)
│
├─ [1] UpdateLoadUnLoadChunkList        (카메라가 청크 경계 넘었을 때만)
│      → m_renderablePosMap 재구성
│      → 새 범위: m_waitLoadChunkPosMap 에 등록
│      → 범위 밖: m_unloadChunkList 에 등록
│
├─ [2] LoadChunks                       (async 디스패치)
│      → 카메라 거리순 정렬 후 std::async(Chunk::Initialize) 발사
│      → m_initFutures 에 <chunk, future> 저장
│
├─ [3] SyncLoadedChunks                 (완료된 future 수확)
│      → future.wait_for(0)으로 논블로킹 확인
│      → 완료된 청크: UpdateChunkGPUBuffer + UpdatePatchChunkMap + m_chunkMap 등록
│      → 이 프레임에 이미 범위 밖이면 곧장 unload 리스트로
│
├─ [4] UnloadChunks
│      → 청크 맵/의존성 자료구조 정리
│      → chunk->Clear() 후 Pool 반환 (GPU 버퍼는 유지)
│
├─ [5] PatchChunks                      (async 디스패치)
│      → 카메라 거리순 정렬 후 std::async(Chunk::Patch) 발사
│      → m_patchFutures 에 <chunk, future> 저장
│
├─ [6] SyncPatchedChunks                (완료된 future 수확)
│      → OnPatchDirtyFlag 일 때만 UpdateChunkGPUBuffer
│
├─ [7] UpdateRenderChunkList            (프러스텀 컬링)
│      → 3종 리스트 재구성: main / mirror / shadow
│
├─ [8] UpdateInstanceInfoList
│      → 렌더 대상 청크의 인스턴스를 형상별로 수집
│      → 형상별 InstanceInfoBuffer 재업로드
│
└─ [9] UpdateChunkConstant              (등장 애니메이션 진행)
       → chunk->Update(dt)로 world 행렬 갱신
       → Constant Buffer 재업로드
```

### 6.1 왜 Load와 Sync를 분리했는가

`LoadChunks`는 **디스패치**만, `SyncLoadedChunks`는 **수확**만 한다. 이렇게 나눈 이유는 두 가지다.

1. **디스패치 상한과 수확 상한이 다르다.** 디스패치는 워커 슬롯이 비었을 때만, 수확은 완료된 future만 처리한다. 하나의 함수로 묶으면 조건 분기가 얽힌다.
2. **완료 처리에 GPU 업로드가 포함된다.** GPU 업로드는 반드시 메인 스레드가 수행해야 하며, 워커에서 넘어온 데이터를 즉시 GPU로 옮기는 지점이 명확히 하나여야 프레임당 GPU 명령 순서 예측이 쉽다.

Patch 쪽도 같은 이유로 `PatchChunks` / `SyncPatchedChunks`로 나뉜다.

### 6.2 UpdateChunkList의 지연 실행

`UpdateLoadUnLoadChunkList`는 매 프레임 호출되지 않는다. `m_isOnChunkUpdateDirtyFlag`가 세워진 프레임에서만 실행된다. 이 플래그는 카메라가 청크 경계를 넘을 때 `OnChunkUpdateDirtyFlag()`로 세워진다. 청크 좌표가 그대로면 로드/언로드 대상이 바뀌지 않으므로 재계산이 무의미하다.

### 6.3 프러스텀 컬링 3종 리스트

`UpdateRenderChunkList`는 모든 활성 청크를 순회하며 세 종류의 프러스텀에 대해 각각 컬링한다.

| 리스트                    | 프러스텀                    | 렌더 대상                                 |
| ------------------------- | --------------------------- | ----------------------------------------- |
| `m_renderChunkList`       | 카메라 프러스텀             | 메인 렌더 (Opaque/SemiAlpha/Transparency) |
| `m_renderShadowChunkList` | 3개 캐스케이드 중 하나 통과 | Shadow Map                                |
| `m_renderMirrorChunkList` | 미러 평면 반사 프러스텀     | 수면 반사                                 |

프러스텀 컬링 알고리즘 자체는 [gpu/FrustumCulling](../../../gpu/FrustumCulling/README.md) 문서에서 다룬다.

### 6.4 각 하위 단계의 상세 문서

- Load/Unload 상세 (의존성 맵, `m_renderablePosMap` 등): [Chunk Load / Unload](../ChunkLoadUnload/README.md)
- Patch 상세 (`m_patchDependencyMap`, `m_lookupDependencySet`, DDA 피킹): [Chunk Patch](../ChunkPatch/README.md)

## 7. 렌더링

Update가 프레임의 앞단이라면, `Render*` 함수들은 뒷단이다. 렌더 파이프라인의 각 패스에서 호출된다.

```
RenderBasic(cameraPos)            — Deferred G-Buffer 채우기 (Opaque/SemiAlpha + Instance)
RenderTransparency()              — Forward 투명 패스 (Water 등)
RenderMirrorWorld()               — 수면 반사 (LowLod + Instance)
RenderBasicShadowMap()            — Cascade Shadow Map (LowLod)
RenderBasicAlbedo()               — SSAO/후처리 등을 위한 albedo-only 패스
```

내부는 대부분 동일한 골격이다: 대응하는 `m_render*ChunkList`를 순회하며 청크마다 `IASetVertexBuffers`/`IASetIndexBuffer`/`VSSetConstantBuffers`로 `m_id` 슬롯의 버퍼를 바인딩하고 `DrawIndexed` 호출.

인스턴스 렌더링(`RenderInstance()`)은 청크 순회가 아니라 **형상 4종**을 순회하며 각각 `DrawIndexedInstanced` 한 번씩 발행한다. 이번 프레임의 형상별 인스턴스 총 개수는 [6-8]에서 이미 `m_instanceInfoBuffers[i]`에 올려져 있다.

메인 렌더 리스트에서 청크와 카메라 거리로 LOD를 분기한다: `Camera::LOD_RENDER_DISTANCE` 초과면 `RenderLowLodChunk`, 아니면 `RenderOpaqueChunk` + `RenderSemiAlphaChunk`.

## 8. 회고

- **싱글턴**은 접근 편의성과 상태 일관성을 얻지만 단위 테스트가 어렵다. 인터페이스 분리 후 의존성 주입으로 바꾸면 렌더 파이프라인·피킹 로직을 독립적으로 테스트할 수 있다.
- **Update 단계가 순차 실행**이다. Load/Patch는 워커에서 병렬화되지만 Update 함수 자체는 한 스레드에서 [1]→[9]를 도는 구조라 각 단계 사이의 병렬화 여지가 있다 (예: RenderList 구성과 InstanceInfo 수집을 병렬로).
- **프러스텀 컬링이 매 프레임 O(N)** 이다. 청크 수가 늘면 여기가 병목이 될 수 있다. 옥트리 등 공간 분할로 개선 여지가 있다.
- **GPU 버퍼 배열 크기가 `CHUNK_POOL_SIZE`로 고정**되어 렌더 거리를 런타임에 늘리기 어렵다. 렌더 거리에 연동하여 풀을 재할당하는 구조가 명확한 다음 개선점이다.
