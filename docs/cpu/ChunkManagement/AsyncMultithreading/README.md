# Async & Multithreading

## 1. 개요

이 문서는 청크 관리에서 **동시성 설계**만 별도로 정리한다.
여기서는 **누가 무엇을 어떤 스레드에서 실행하며, 왜 그렇게 나눴는가**를 짚는다.

각 청크가 무엇을 계산하는지, 어떤 자료구조를 채우는지는 다른 문서 참고.

## 2. 스레드 모델

```
메인 스레드 ─────────────────────────────────────────────────────
│  Update() 호출
│  ├─ Load/Patch 디스패치      (std::async 쓰레드 실행)
│  ├─ 완료된 future 동기화      (wait_for(0us) 논블로킹)
│  ├─ CPU 버퍼 count 스냅샷    (UpdateCpuBufferCount)
│  ├─ GPU 리소스 업로드         (Resize/Update Buffer)
│
├─ Init 워커 (1~3개)
│  └─ Chunk::Initialize      (지형/바이옴/블록/나무/인스턴스/메시)
│
└─ Patch 워커 (1~2개)
   └─ Chunk::Patch           (블록 교체 + 전체 재메싱)
```

메인 스레드는 **지휘자** 역할만 한다: 워커에 일을 던지고, 완료된 결과를 GPU에 올리고, 렌더를 지시한다. **CPU 데이터를 실제로 만드는 것은 워커**가 다룬다.

## 3. 스레드 카운트 산정

`ChunkManager::Initialize()`가 시작될 때 결정된다.

```cpp
void ChunkManager::InitWorkerThreadCount()
{
	int maxThreadCount = std::thread::hardware_concurrency();
	if (maxThreadCount == 0) // 검출 실패
		maxThreadCount = 4;

	const int reservedThreadCount = 2;
	int workerThreadCount = max(2, maxThreadCount - reservedThreadCount);

	m_patchThreadCount = max(1u, workerThreadCount / 2);
	m_initThreadCount = max(1u, workerThreadCount - m_patchThreadCount);
}
```

- `maxThreadCount`: 1~2개인 경우, 최솟값만 잡아두고 그대로 실행 (이러한 경우는 거의 없겠지만, OS 스케쥴러가 알아서 동작하게 될 것)
- `reservedThreadCount`: main 쓰레드, GPU 드라이버 쓰레드, 기타 등등의 이유로 여유분을 2개 잡는다.
- `workerThreadCount`: 최소한 2개를 잡아둔다.

## 4. std::async: 현재 시스템은 잡 시스템도 아니고, 직접 쓰레드 풀 패턴을 사용하지 않는다.

`std::async(std::launch::async, ...)`는 실행 시 매번 쓰레드를 새로 만든다로 알고 있지만 사실 MSVC 환경에서는 그렇지 않다고 한다.

- **MSVC** 표준 라이브러리에서 `std::async(launch::async, ...)`는 **Windows Thread Pool** (Concurrency Runtime / PPL) 위에서 스케줄된다. 매번 `CreateThread` 호출이 발생하지 않는다.
- Voxen은 Windows + MSVC 전용 프로젝트다. 이 특성에 기대는 것이 문제되지 않는다.

즉 **쓰레드 풀 없이 쓰레드를 매번 만드는 비용**은 실제로 발생하지 않는다. 잡 시스템/자체 스레드 풀 없이도 `std::async`만으로 실용적인 풀 기반 실행이 얻어진다.

**이로 인한 한계**

- **거리 기반 우선순위 스케줄 불가** — 디스패치로 넘길 순서 자체를 정렬하고 있는 것이지 쓰레드 끼리의 스케쥴링을 실제로 하고 있지는 않다.
- **취소 불가** — 렌더 거리에 벗어난 청크인데 로드를 하는 경우 취소를 하지 못한다. (동기화 시 재검사 진행)

## 5. Load/Sync 분리 — 디스패치와 동기화

`LoadChunks`(디스패치)와 `SyncLoadedChunks`(수확)는 분리하여 생각했다. (`PatchChunks` / `SyncPatchedChunks`도 마찬가지)

- `LoadChunks()`: 워커 슬롯 체크 후 워커 실행
- `SyncLoadedChunks()`: 워커 완료 체크 후 GPU 버퍼 업데이트

```cpp
void ChunkManager::Update(float dt, Camera& camera, const Light& light)
{
    ...

	LoadChunks(camera);
	SyncLoadedChunks();

	...

	PatchChunks(camera);
	SyncPatchedChunks();

	...
}
```

## 6. ChunkLoadMemory 풀링

`ChunkLoadMemory`는 워커가 청크 초기화·패치 시 필요한 임시 버퍼 집합이다. 노이즈 배열 7종(각 34×34), 컬럼 비트 배열 4종(각 34² × 6 slots), 바이옴 맵, 나무/인스턴스 배치 후보 vector 등 도합 수백 KB에 달한다.

**왜 풀링인가:**

- **공용 메모리를 사용할 수 없음** — 멀티쓰레드로 초기화를 레이스컨디션을 피하기 위해 뮤텍스를 사용하기엔 워커 비용에 많은 부담이 되었다.
- **스택 오버플로우 방지** — 워커 스택에 잡을 수 없는 크기.
- **매 로드마다 힙 할당 회피** — 로드가 빈번하고, 할당 자체가 프레임 스파이크의 원인이 될 수 있다.

**풀 크기 = `m_initThreadCount + m_patchThreadCount`.**
동시 실행될 수 있는 워커 수만큼만 있으면 충분하다. 이보다 많으면 낭비, 적으면 워커가 대여 실패로 다음 프레임에 맡긴다.

- Pool에서 할당 받을 시 풀 고갈 체크도 진행하는데, 풀 고갈 시에도 별다른 예외 처리 없이 조용히 넘어가고, 다음 프레임에 재시도 하게 된다.

## 7. DX11 스레드 제약

D3D11 Immediate Context는 **단일 스레드 접근**만 허용한다.

워커에서 `context`에 접근해 `Map`/`Unmap`/`CreateBuffer`를 호출하면 미정의 동작이 발생한다.

이를 위해 GPU 관련 로직 자체를 ChunkManager로 두고 Chunk 내부에서는 GPU 관련 연산 자체를 하지 않도록 데이터 자체를 분리했다.

사실 CPU<->GPU 분리는 멀티쓰레드 환경을 고려했다기보다는, Instance Rendering을 구현하다가 얻게된 효과다.

- ChunkManager에서 Instance 관련된 GPU를 모아야 했음
- Chunk는 Instance에 관련된 GPU 데이터가 필요없어짐
- Chunk 내부에 Block(CPU + GPU), Instance(CPU) 이렇게 존재하는 것보다, Chunk는 CPU 데이터만, ChunkManager는 GPU 데이터를 중심으로 분리하다보니 워커가 Context에 접근하지 않는 로직이 구성 되었다.

cf. Deferred Context의 존재를 모르고 있었다.

- Deferred Context는 쉽게 GPU가 바로 처리 가능한 명령을 모아둔 버퍼 -> 워커에서 생성해서 사용
- 메인 쓰레드가 커맨드를 생성하는 일을 줄여줌
- 멀티쓰레드 기반 렌더링은 아님. 멀티쓰레드 기반으로 커맨드를 생성하여 Immediate Context로 보내 렌더링 되는 것

## 8. 뮤텍스 없이 얻은 스레드 안전성

Voxen의 청크 관리에는 `std::mutex`, `std::semaphore`, `std::atomic` 같은 명시적 동기화 프리미티브가 사용된 곳이 거의 없다. 락으로 임계 구역을 보호하는 대신 **애초에 스레드끼리 같은 메모리를 만지지 않게** 설계했다.

### 8.1 원칙 — "공유하지 않으면 락도 필요 없다"

멀티스레딩에서 락은 **레이스 컨디션**을 해결하는 도구다. 반대로 각 스레드가 **자기만의 데이터**를 다루도록 만들면 락은 필요조차 없어진다.

각 워커가 **자기만의 임시 버퍼**를 소유한다. (§8.2)

CPU 데이터와 GPU 데이터는 **서로 다른 소유자**가 다룬다. (§8.3)

### 8.2 ChunkLoadMemory — 워커당 독립 대여

`ChunkLoadMemory` 하나엔 노이즈 배열, 컬럼 비트 배열, 바이옴 맵, 나무/인스턴스 후보 vector 등 수백 KB의 임시 버퍼가 담긴다. 만약 이걸 **글로벌 하나**로 두고 워커들이 공유했다면, 매 필드마다 락이 필요했을 것이다. (스택X, 동적할당 느림)

메모리 풀에 미리 할당해놓는다.

```cpp
void ChunkManager::InitChunkLoadMemoryPool()
{
    for (unsigned int i = 0; i < m_initThreadCount + m_patchThreadCount; ++i)
        m_chunkLoadMemoryPool.push_back(new ChunkLoadMemory());
}
```

워커는 시작 시점에 하나를 대여한다.

```cpp
ChunkLoadMemory* mem = GetChunkLoadMemoryFromPool();
// ...
m_initFutures.push_back({chunk,
    std::async(std::launch::async, &Chunk::Initialize, chunk, pos, mem)});
```

- 대여받은 워커는 그 인스턴스를 **혼자만** 만진다.
- 다른 워커는 다른 인스턴스를 대여받는다.
- 반환은 워커 완료 후 메인 스레드 Sync 함수 안에서 수행한다.

즉 `ChunkLoadMemory`는 어느 시점에도 두 스레드가 동시에 만지지 않는다. 락 없이 안전하다.

풀 자체의 대여/반환은 메인 스레드에서만 일어난다는 점도 중요하다. `GetChunkLoadMemoryFromPool` / `ReleaseChunkLoadMemoryToPool` 호출자는 항상 메인이므로 풀 컨테이너(`m_chunkLoadMemoryPool`)도 자동으로 단일 스레드 접근이 된다.

소유권 락 없이 사용하기에 좋고, 메모리를 동적으로 할당하지 않는다는 장점도 존재하지만 단점도 존재한다.

- 풀 대여/반환이 RAII를 지키지 않으면 누수가 발생할 수 있다.
- 사용중이지 않는 상황이라도 풀 자체의 메모리 크기를 크게 잡고 있다.

단점보다는 장점에 집중하여 위와 같은 방식으로 메모리를 할당하여 대여/반환식으로 워커 메모리 독립을 구성했다.

### 8.3. CPU 데이터와 GPU 데이터의 소유자 분리

**GPU 버퍼가 Chunk가 아닌 ChunkManager에 있는 것**([ChunkManager §4](../ChunkManager/README.md))이 이 분리에 결정적이다. 워커는 Chunk 인스턴스를 다루지만, D3D11 리소스를 만질 방법이 없다. DX11 Immediate Context의 단일 스레드 제약이 자동으로 지켜진다.

렌더 스레드(=메인)가 Chunk의 CPU vector를 만지지 않는다는 것도 같은 축이다. `DrawIndexed(chunk->GetOpaqueIndexCount(), 0, 0)`는 count 필드만 조회하고 GPU 버퍼만 바인딩한다. 워커가 다음 프레임에서 vector를 재구성 중이더라도 렌더가 참조하는 데이터는 완전히 별개다.

### 8.4 이 설계로 얻는 장점과 단점

장점

- 공유된 메모리가 없기에 로직 작성에 편리하다.
- 뮤텍스 락 언락에 대한 대기 문제없어 빠르다.

단점

- 풀 대여/반환이 RAII를 지키지 않으면 누수가 발생할 수 있다.
- 사용중이지 않는 상황이라도 풀 자체의 메모리 크기를 크게 잡고 있다.
- 규모가 더 커지면 실수하기 쉽다. 컴파일러가 동시성에 대한 에러를 잡아주는 상황이 아닌, 사용자가 임의로 `이건 독립 메모리니까 동시성에 문제가 없어` 식이다.

## 9. 회고

- std::async, std::future를 처음 이용해본 프로젝트
- 워커가 어디서부터 작업을 실행 시켜야 하는지, 실행해도 인자를 잘 받아서 초기화에 문제가 없는지 등 문제가 많았던 챕터
- 멀티쓰레드로 실행을 시켰다한들, 프로그램 전체에 크래쉬가 없는지 불안한 점은 존재
- 쓰레드 풀을 사용하냐마냐가 중요한게 아니라, 멀티 쓰레드 관리 전반을 위해서는 "잡시스템"을 구현해야한다고 함
  - 잡시스템은 하나의 프로젝트로서 범위가 넓고 어려운 내용들이 존재한다고 함
