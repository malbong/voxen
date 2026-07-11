# Async & Multithreading

## 1. 개요

이 문서는 청크 관리 시스템의 **동시성 설계**만 별도로 정리한다. 각 청크가 무엇을 계산하는지, 어떤 자료구조를 채우는지는 [Chunk Structure](../ChunkStructure/README.md), [ChunkManager](../ChunkManager/README.md), [Chunk Load/Unload](../ChunkLoadUnload/README.md), [Chunk Patch](../ChunkPatch/README.md)에서 이미 다뤘다. 여기서는 **누가 무엇을 어떤 스레드에서 실행하며, 왜 그렇게 나눴는가**를 짚는다.

## 2. 스레드 모델

```
메인 스레드 ─────────────────────────────────────────────────────
│  Update() 호출
│  ├─ Load/Patch 디스패치      (std::async 발사만)
│  ├─ 완료된 future 수확       (wait_for(0us) 논블로킹)
│  ├─ CPU 버퍼 count 스냅샷    (UpdateCpuBufferCount)
│  ├─ GPU 리소스 업로드         (Resize/Update Buffer)
│  ├─ 프러스텀 컬링            (렌더 리스트 구성)
│  └─ 렌더 함수 호출            (DrawIndexed 등)
│
├─ Init 워커 (1~3개)
│   └─ Chunk::Initialize          (지형/바이옴/블록/나무/인스턴스/메시)
│
└─ Patch 워커 (1~2개)
    └─ Chunk::Patch                (블록 교체 + 전체 재메싱)
```

메인 스레드는 **오케스트라 지휘자** 역할만 한다: 워커에 일을 던지고, 완료된 결과를 GPU에 올리고, 렌더를 지시한다. **CPU 데이터를 실제로 만드는 것은 워커 전담**이다.

## 3. 스레드 카운트 산정

`ChunkManager::Initialize()`가 시작될 때 결정된다.

```cpp
uint32_t maxThreads     = min(6u, std::thread::hardware_concurrency());
uint32_t usableThreads  = (maxThreads > 1) ? maxThreads - 1 : 1;    // 메인용 1개 예약
m_initThreadCount  = std::clamp(usableThreads - 1u, 1u, 3u);        // Init 워커: 1~3
m_patchThreadCount = std::clamp(usableThreads - m_initThreadCount, 1u, 2u); // Patch: 1~2
```

- **`min(6, hw_concurrency)`** — 상한 6. Voxen이 코어 다수를 독점하지 않게 한다. 브라우저/OS 등 백그라운드에 여유를 남긴다.
- **`- 1`** — 메인 스레드 몫으로 예약. 메인이 렌더/입력 등 지연 민감 작업을 하므로 워커에 밀리면 안 된다.
- **Init 우선 배분** — Init이 대체로 Patch보다 무겁고 (지형·바이옴·나무 배치 + 메시), 로드 시 대량 대기가 흔하므로 여기에 슬롯을 먼저 준다.
- **`clamp`** — 저사양(듀얼코어) 환경에서도 각 최소 1개는 보장.

## 4. 왜 std::async를 유지했는가

첫 인상으로 `std::async(std::launch::async, ...)`는 "매번 새 스레드를 만드는" 것으로 오해되기 쉽다. 실제로 다음이 사실이다.

- **MSVC** 표준 라이브러리에서 `std::async(launch::async, ...)`는 **Windows Thread Pool** (Concurrency Runtime / PPL) 위에서 스케줄된다. 매번 `CreateThread` 호출이 발생하지 않는다.
- Voxen은 Windows + MSVC 전용 프로젝트다. 이 특성에 기대는 것이 문제되지 않는다.

즉 **"쓰레드 풀 없이 쓰레드를 매번 만드는 비용"**은 실제로 발생하지 않는다. 잡 시스템/자체 스레드 풀 없이도 `std::async`만으로 실용적인 풀 기반 실행이 얻어진다.

### 4.1 감내한 한계

std::async는 잡 시스템의 이점을 가지지 못한다.

- **거리 기반 우선순위 스케줄 불가** — 카메라 근처 청크를 먼저 완료하려면 별도 우선순위 큐가 필요하다. Voxen은 이를 **디스패치 시점 정렬**로 대체한다 (`SortPosListByCameraDistance` 후 `std::async` 발사). 워커 안의 실행 순서는 여전히 시스템 스케줄러에 맡긴다.
- **잡 취소 불가** — 발사한 후 결과가 필요 없어져도 워커는 끝까지 실행한다. 카메라가 반대편으로 순간이동하면 이미 발사한 청크는 완료 후 곧장 언로드 큐로 밀린다 (`SyncLoadedChunks`의 즉시 언로드 경로).
- **future의 destructor blocking** — `std::async`의 future를 임시로 두면 소멸자에서 완료를 블로킹한다. Voxen은 반환값을 반드시 `m_initFutures` / `m_patchFutures`에 저장하여 이 함정을 회피한다.

### 4.2 언제 갈아탈 것인가

현재 구성은 다음 신호가 나타나면 재검토한다.

- 렌더 거리 확장 시 초기 로드 스파이크가 눈에 띔 → 우선순위 큐 도입 신호.
- 플레이 중 카메라 급전환이 잦아 취소 못 하는 작업의 낭비가 커짐 → 잡 취소 도입 신호.
- 크로스 플랫폼 요구 등장 → libstdc++/libc++ 환경에서 `std::async`는 매번 스레드 생성이므로 자체 풀 필요.

이 신호들이 없는 한 std::async 유지가 최소 복잡도 해답이다.

## 5. Load/Sync 분리 — Dispatch와 Harvest

`LoadChunks`(디스패치)와 `SyncLoadedChunks`(수확)는 하나로 합칠 수 있었음에도 나뉘어 있다. 이유는 두 가지.

**(a) 상한 조건이 다르다.**
디스패치는 워커 슬롯이 비었을 때만 진행한다.
수확은 완료된 future만 처리하며, 완료 개수에 상한이 없다.
하나로 묶으면 조건 흐름이 얽힌다.

**(b) GPU 업로드 지점을 하나로 고정.**
수확 단계에서 즉시 `UpdateChunkGPUBuffer`가 호출된다. GPU 업로드는 반드시 메인 스레드여야 하고 (§7), 프레임당 GPU 명령 순서를 예측 가능하게 하려면 이 지점이 명확히 한 곳이어야 한다.

같은 이유로 `PatchChunks` / `SyncPatchedChunks`도 나뉜다.

## 6. ChunkLoadMemory 풀링

`ChunkLoadMemory`는 워커가 청크 초기화·패치 시 필요한 임시 버퍼 집합이다. 노이즈 배열 7종(각 34×34), 컬럼 비트 배열 4종(각 34² × 6 slots), 바이옴 맵, 나무/인스턴스 배치 후보 vector 등 도합 수백 KB에 달한다.

**왜 풀링인가:**

- **스택 오버플로우 방지** — 워커 스택에 잡을 수 없는 크기.
- **매 로드마다 힙 할당 회피** — 로드가 빈번하고, 할당 자체가 프레임 스파이크의 원인이 될 수 있다.
- **RAII 유사한 대여/반환 규칙** — `GetChunkLoadMemoryFromPool()`로 대여, 작업 종료 후 `Clear()` → `ReleaseChunkLoadMemoryToPool()`로 반환. Sync 함수는 반환 전에 필요한 부분(`loadPatchResult`)만 `std::move`로 회수한다.

**풀 크기 = `m_initThreadCount + m_patchThreadCount`.**
동시 실행될 수 있는 워커 수만큼만 있으면 충분하다. 이보다 많으면 낭비, 적으면 워커가 대여 실패로 대기 상태에 빠진다.

풀 고갈은 **정상적인 백프레셔 신호**로 취급된다. 대여 실패한 워커 작업은 조용히 다음 프레임으로 재시도된다. 별도 예외 처리 없이 파이프라인이 스스로 부하를 억제한다.

## 7. DX11 스레드 제약과 스레드 경계

D3D11 Immediate Context는 **단일 스레드 접근**만 허용한다 (Deferred Context는 별개 이야기지만 Voxen은 사용하지 않는다). 워커에서 `Map`/`Unmap`/`CreateBuffer`를 호출하면 미정의 동작이 발생한다.

Voxen의 스레드 경계는 이 제약을 정면으로 반영한다.

```
워커 스레드                          메인 스레드
─────────────────────────────────────────────────────────
Chunk::Initialize                    SyncLoadedChunks
Chunk::Patch                         ├─ UpdateCpuBufferCount
├─ m_blocks[] 채우기                 ├─ UpdateChunkGPUBuffer  ← DX11 호출은 여기서만
├─ m_instanceMap 채우기              └─ 렌더 함수들
├─ CPU vector 채우기 (Greedy Meshing)
└─ ChunkLoadMemory 활용
```

**Chunk가 GPU 리소스를 직접 소유하지 않는다는 결정** ([ChunkManager §4](../ChunkManager/README.md))이 이 경계를 자연스럽게 만든다. 워커는 Chunk를 만지지만 D3D11 리소스는 만질 방법이 없다.

### 7.1 count 스냅샷과 1 frame flickering

워커가 vector를 재구성하는 중에 메인 스레드가 `vector::size()`를 읽으면 데이터 레이스가 발생한다. 이걸 해결하는 게 `UpdateCpuBufferCount()` 스냅샷 패턴이다. 상세는 [Chunk Patch §7](../ChunkPatch/README.md)에서 다룬다.

요점만 재확인:

- 렌더 시점엔 `vector::size()`가 아니라 별도 count 필드(`m_opaqueIndexCount` 등)를 조회한다.
- 이 count는 Sync 함수 안에서 명시적으로 갱신된다 — 워커 작업이 완료된 직후, 다음 워커가 시작되기 전.
- GPU 버퍼도 같은 시점에 업로드되므로 count와 GPU 데이터가 어긋나지 않는다.

## 8. 뮤텍스 없이 얻은 스레드 안전성 — 독립 메모리와 소유권 이전

Voxen의 청크 관리에는 `std::mutex`, `std::semaphore`, `std::atomic` 같은 명시적 동기화 프리미티브가 사용된 곳이 거의 없다. 락으로 임계 구역을 보호하는 대신 **애초에 스레드끼리 같은 메모리를 만지지 않게** 설계했다.

### 8.1 원칙 — "공유하지 않으면 락도 필요 없다"

멀티스레딩에서 락은 **공유 자원의 경쟁적 접근**을 해결하는 도구다. 반대로 각 스레드가 **자기만의 데이터**를 다루도록 만들면 락은 필요조차 없어진다. 이 문서 시스템은 다음 네 지점에서 이 원칙을 관철했다.

1. 각 워커가 **자기만의 임시 버퍼**를 소유한다 (§8.2).
2. 워커에 넘긴 청크는 워커가 끝날 때까지 **메인이 만지지 않는다** (§8.3).
3. CPU 데이터와 GPU 데이터는 **서로 다른 소유자**가 다룬다 (§8.4).
4. 스레드 간 데이터 인수인계는 **이동 시맨틱**으로 소유권을 명확히 넘긴다 (§8.5).

각각을 살펴본다.

### 8.2 ChunkLoadMemory — 워커당 독립 대여

`ChunkLoadMemory` 하나엔 노이즈 배열, 컬럼 비트 배열, 바이옴 맵, 나무/인스턴스 후보 vector 등 수백 KB의 임시 버퍼가 담긴다. 만약 이걸 **글로벌 하나**로 두고 워커들이 공유했다면, 매 필드마다 락이 필요했을 것이다.

Voxen은 반대로 간다.

```cpp
void ChunkManager::InitChunkLoadMemoryPool()
{
    for (unsigned int i = 0; i < m_initThreadCount + m_patchThreadCount; ++i)
        m_chunkLoadMemoryPool.push_back(new ChunkLoadMemory());
}
```

동시에 실행될 수 있는 워커 수만큼 **미리 할당**해 두고, 워커는 시작 시점에 하나를 대여한다.

```cpp
ChunkLoadMemory* mem = GetChunkLoadMemoryFromPool();
// ...
m_initFutures.push_back({chunk,
    std::async(std::launch::async, &Chunk::Initialize, chunk, pos, mem)});
```

- 대여받은 워커는 그 인스턴스를 **혼자만** 만진다.
- 다른 워커는 다른 인스턴스를 대여받는다.
- 반환은 워커 완료 후 메인 스레드가 Sync 함수 안에서 수행한다.

즉 `ChunkLoadMemory`는 어느 시점에도 두 스레드가 동시에 만지지 않는다. 락 없이 안전하다.

풀 자체의 대여/반환은 메인 스레드에서만 일어난다는 점도 중요하다. `GetChunkLoadMemoryFromPool` / `ReleaseChunkLoadMemoryToPool` 호출자는 항상 메인이므로 풀 컨테이너(`m_chunkLoadMemoryPool` vector)도 자동으로 단일 스레드 접근이 된다.

### 8.3 Chunk 자체의 배타적 소유권 이전

같은 원리가 `Chunk*` 자체에도 적용된다. 워커에 넘긴 청크는 **완료될 때까지 메인이 만지지 않는다.**

```cpp
// 디스패치 시점 — Chunk 포인터를 워커에 넘기고 future에 보관
m_initFutures.push_back({chunk,
    std::async(&Chunk::Initialize, chunk, pos, mem)});

// 수확 시점 — future가 완료되어야만 다시 Chunk에 접근
if (it->second.wait_for(0us) != std::future_status::ready) { ++it; continue; }
Chunk* chunk = it->first;   // 이제 다시 메인이 만짐
```

두 프로토콜이 이 배타성을 보장한다.

**(a) 상태 플래그로 재발사 금지.**

```cpp
if (chunk->IsPatching()) continue;   // PatchChunks 안
```

이미 워커가 붙잡고 있는 청크에는 새 작업을 발사하지 않는다. Patch 워커가 실행 중인 청크에 두 번째 워커가 붙는 상황을 원천 차단한다.

**(b) 청크 재활용은 완료 후에만.**

Chunk Pool에서 대여된 청크는 Sync 완료 → 활성 사용 → Unload → Pool 반환 → 재대여 순으로 순환한다. 워커가 아직 붙잡고 있는 청크는 Pool에 돌아갈 수 없다. 재대여 자체가 불가능하다는 뜻이다.

이 두 규칙 덕에 `Chunk` 인스턴스도 어느 순간에도 두 스레드가 동시에 소유하지 않는다.

### 8.4 CPU 데이터와 GPU 데이터의 소유자 분리

한 걸음 더 나아가, Chunk 안에서도 워커와 메인이 만지는 필드가 갈린다.

| 필드 | 만지는 주체 | 언제 |
| --- | --- | --- |
| `m_blocks[][][]`, `m_instanceMap`, CPU vector 4종 | 워커 | Initialize/Patch 실행 중 |
| CPU count 4종 (`m_opaqueIndexCount` 등) | 메인 | Sync에서 스냅샷 |
| `m_isLoaded`, `m_isPatching` 등 상태 플래그 | 메인 | Sync 완료 후 세팅 |
| GPU 버퍼 (`m_opaqueVertexBuffers[id]` 등) | 메인 | `UpdateChunkGPUBuffer` |

**GPU 버퍼가 Chunk가 아닌 ChunkManager에 있는 것**([ChunkManager §4](../ChunkManager/README.md))이 이 분리에 결정적이다. 워커는 Chunk 인스턴스를 다루지만, D3D11 리소스를 만질 방법이 없다. DX11 Immediate Context의 단일 스레드 제약이 자동으로 지켜진다.

렌더 스레드(=메인)가 Chunk의 CPU vector를 만지지 않는다는 것도 같은 축이다. `DrawIndexed(chunk->GetOpaqueIndexCount(), 0, 0)`는 count 필드만 조회하고 GPU 버퍼만 바인딩한다. 워커가 다음 프레임에서 vector를 재구성 중이더라도 렌더가 참조하는 데이터는 완전히 별개다.

### 8.5 소유권 인수인계는 이동 시맨틱으로

스레드 간 데이터 전달이 아예 없을 수는 없다. 워커가 만든 결과 중 일부는 결국 메인이 이어받아야 한다. Voxen은 이 인계를 **`std::move`**로 명시한다.

**loadPatchResult 회수:**

```cpp
Chunk* chunk = it->first;
ChunkLoadMemory* mem = it->second.get();   // future에서 소유권 회수

// 메모리를 풀에 되돌리기 전, 필요한 부분만 move로 뽑아둠
PosHashMap<PatchDataHashSet> loadPatchResult(std::move(mem->loadPatchResult));
ReleaseChunkLoadMemoryToPool(mem);          // 이제 mem은 다음 워커의 것
```

`std::move` 이후 `mem->loadPatchResult`는 빈 상태로 남고, 실제 데이터의 소유는 지역 변수로 넘어온다. 이후엔 오직 이 지역 변수만 사용된다.

**waitPatchChunkMap 발사:**

```cpp
m_patchFutures.push_back({chunk,
    std::async(&Chunk::Patch, chunk,
               std::move(m_waitPatchChunkMap[chunkPos]),   // ← 워커로 소유권 이전
               mem)});
m_waitPatchChunkMap.erase(chunkPos);
```

`waitPatchChunkMap`의 해시셋을 워커에 넘기는 순간 매니저 쪽에는 빈 컨테이너만 남고 곧이어 지워진다. 이후 이 데이터의 유일한 소유자는 워커다.

이동 시맨틱은 **"이 시점 이후로 이쪽은 이 데이터를 만지지 않겠다"**를 코드로 표현한 것이다. 락 없이도 접근 중복을 사전 차단하는 언어 차원의 도구다.

### 8.6 std::future — 표준이 제공하는 동기화

명시적 뮤텍스가 없더라도 스레드 간 **완료 통지**는 필요하다. Voxen은 이걸 `std::future`에 위임한다.

- `std::async`가 반환한 future의 shared state는 **표준이 스레드 안전을 보장**한다.
- `wait_for(0us)`는 논블로킹으로 완료 여부만 조회한다. 락 대기에 프레임을 뺏기지 않는다.
- `future.get()`은 완료된 결과를 안전하게 회수하며, 이후 shared state는 소진된다.

즉 락과 세마포어의 역할을 표준 라이브러리가 이미 하고 있다. Voxen 코드에는 그 위의 얇은 프로토콜만 남는다.

### 8.7 이 설계가 감내한 트레이드오프

락 없는 설계가 공짜는 아니다.

- **동시성이 프로토콜에 갇힌다.** "워커 실행 중에는 그 청크를 만지지 않는다", "이동 시맨틱으로 인계한다" 같은 규칙이 코드 배치로 지켜지지, 컴파일러가 강제하지는 않는다. 실수로 규칙을 깨는 커밋을 정적 분석으로 잡기 어렵다.
- **워커 간 데이터 공유가 필요해지면 처음부터 다시 설계해야 한다.** 예를 들어 워커끼리 나무 정보를 실시간으로 교환해야 하는 상황이 생기면 지금 구조는 성립하지 않는다. 그때는 잡 시스템 또는 명시적 동기화 도입이 불가피하다.

현재는 청크 관리라는 도메인 특성상 워커 간 소통이 없어도 문제가 해결되므로 이 트레이드오프가 성립한다. 도메인이 요구하지 않는데 락을 얹지 않은, 근거 있는 결정이다.

## 9. 회고

- **현업 표준 답은 "잡 시스템"이다.** Frostbite / Naughty Dog의 fiber 기반 잡 시스템, Unreal의 TaskGraph, Unity DOTS Job System 등은 우선순위, 종속성 DAG, 잡 취소, 워크 스틸링을 모두 통합한다. Voxen이 현업 규모로 확장된다면 자연스러운 다음 단계는 이 방향이다.
- **하지만 현재 std::async 구성을 유지하는 것은 근거 있는 결정이다.** MSVC 특성으로 풀 기반 실행이 이미 얻어지고, 우선순위와 취소가 실사용에서 병목으로 드러나지 않는다. 잡 시스템 도입은 그 자체가 별도 프로젝트 규모의 스코프를 요구한다.
- **Load/Sync 분리와 count 스냅샷**은 동시성 실수를 사전 차단하는 두 축이다. 이 두 규칙이 없으면 사소한 함수 재배치만으로도 데이터 레이스가 살아난다. 규칙을 코드로 강제하는 구조(예: 별도 클래스로 캡슐화)를 도입할 여지가 있다.
- **관측성 부족** — 풀 고갈이 얼마나 자주 발생하는지, 어느 워커가 병목인지 지표가 없다. 카운터·로그 추가는 프로파일링/튜닝의 명확한 다음 단계다.
