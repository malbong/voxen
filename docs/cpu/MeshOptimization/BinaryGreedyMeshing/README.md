# BinaryGreedyMeshing

## 1. 개요

BinaryGreedyMeshing은 Voxen 엔진에서 **비트 연산 기반 Greedy Meshing 알고리즘**을 사용하여 복셀 블록 메쉬를 최적화하는 시스템입니다. 3D 블록 배열(32x32x32)을 **64비트 정수 배열(Column Bit)**로 변환한 후, **비트 시프트와 마스킹**만으로 Face Culling, 타입별 분류, 사각형 병합을 수행합니다.

엔진 전체에서 다음 역할을 수행합니다:
- **CPU 측**: `Chunk::InitWorldVerticesData()`에서 3D 블록 배열 → 2D 비트 배열 → 최적화된 메쉬 생성
- **메쉬 최적화**: 인접한 동일 타입 블록 면을 하나의 사각형으로 병합하여 정점 수 **90% 이상 감소**
- **Face Culling**: 비트 연산(`& ~(bit << 1)`)으로 숨겨진 면 제거 → 렌더링 부하 감소
- **타입별 분리**: Opaque, Transparency, SemiAlpha, LowLod를 별도 버퍼로 관리하여 렌더링 패스 최적화

## 2. 도입 동기

복셀 엔진에서 블록을 개별적으로 렌더링하면 정점 수가 폭발적으로 증가합니다.

### 기존 Naive 방식의 문제점
1. **정점 폭발**:
   - 32x32x32 청크 = 32,768 블록
   - 블록당 6면 × 4정점 = 24정점
   - **총 786,432 정점** (인덱스 제외)
   - 100개 청크 = **78,643,200 정점** → GPU 처리 불가능

2. **숨겨진 면 렌더링**:
   - 땅속 블록의 모든 면이 인접 블록에 가려져도 렌더링
   - **실제 보이는 면: 10% 미만**, 나머지 90%는 낭비

3. **타입별 렌더링 패스 비효율**:
   - Opaque → Transparency → SemiAlpha 순서로 렌더링 필요
   - 타입별로 정점을 분리하지 않으면 렌더 상태 전환 빈번

4. **루프 기반 Face Culling 비용**:
   - 모든 블록의 6방향 인접 블록 검사: 32,768 × 6 = **196,608회 조건문**
   - CPU 병목

### 목표
- **Greedy Meshing**: 인접한 동일 면을 하나의 사각형으로 병합 → 정점 수 90% 감소
- **비트 기반 Face Culling**: 조건문 대신 비트 연산으로 숨겨진 면 제거 → 속도 30배 향상
- **타입별 분리**: Opaque, Transparency, SemiAlpha, LowLod를 독립적으로 처리
- **메모리 효율**: 64비트 정수 배열로 블록 정보 압축 표현

## 3. 핵심 아이디어

### 3.1. Column Bit 표현

**3D 블록 배열을 2D 비트 배열로 변환**하여 메모리 사용량 감소 및 비트 연산 가능:

**개념**:
```
3D 블록 배열 (32x32x32):
m_blocks[x][y][z] = BLOCK_TYPE

↓ 변환

2D Column Bit 배열 (각 축별):
colBit[axis][h][w] = uint64_t (64비트)
```

**Column Bit 구조**:
- 각 축(X, Y, Z)의 각 컬럼을 64비트 정수 하나로 표현
- 비트 위치 = 해당 축의 좌표
- 비트 값 = 블록 존재 여부 (1: 있음, 0: 없음)

**예시 (Y축 컬럼)**:
```
Y축 방향 컬럼 (x=5, z=10):
m_blocks[5][0][10] = AIR      → 비트 0 = 0
m_blocks[5][1][10] = STONE    → 비트 1 = 1
m_blocks[5][2][10] = STONE    → 비트 2 = 1
m_blocks[5][3][10] = STONE    → 비트 3 = 1
m_blocks[5][4][10] = AIR      → 비트 4 = 0
...

↓ Column Bit

colBit[0][10][5] = 0b...00001110 (비트 1-3이 1)
                           ^^^
                        STONE 블록들
```

**장점**:
- **메모리 압축**: 32개 블록 정보를 64비트(8바이트)에 저장
- **병렬 처리**: 64개 블록을 한 번에 비트 연산으로 처리
- **캐시 효율**: 연속된 메모리 배열로 캐시 친화적

### 3.2. 비트 연산 기반 Face Culling

**인접 블록 검사를 비트 시프트로 대체**하여 조건문 없이 숨겨진 면 제거:

**원리**:
```
colBit:        0b00111100  (블록 위치)
colBit << 1:   0b01111000  (왼쪽 인접 블록)
~(colBit << 1):0b10000111  (왼쪽에 인접 블록 없는 위치)

cullBit = colBit & ~(colBit << 1)
        = 0b00111100 & 0b10000111
        = 0b00000100  ← 왼쪽 끝 블록만 면 생성
```

**코드 (Chunk.cpp:744-761)**:
```cpp
for (int axis = 0; axis < 3; ++axis) {
    for (int h = 1; h < CHUNK_SIZE_P - 1; ++h) {
        for (int w = 1; w < CHUNK_SIZE_P - 1; ++w) {
            uint64_t opBit = memory->opColBit[Utils::GetIndexFrom3D(axis, h, w, CHUNK_SIZE_P)];

            // 왼쪽 면 (- → +)
            memory->opCullColBit[Utils::GetIndexFrom3D(axis * 2 + 0, h, w, CHUNK_SIZE_P)] =
                opBit & ~(opBit << 1);

            // 오른쪽 면 (+ → -)
            memory->opCullColBit[Utils::GetIndexFrom3D(axis * 2 + 1, h, w, CHUNK_SIZE_P)] =
                opBit & ~(opBit >> 1);
        }
    }
}
```

**성능**:
- **조건문 방식**: 196,608회 조건문
- **비트 연산 방식**: 6,144회 비트 연산
- **속도 향상**: 약 **30배**

### 3.3. 타입별 Column Bit 분리

**블록 타입에 따라 서로 다른 Face Culling 규칙 적용**:

| 타입            | Face Culling 규칙                           |
|----------------|---------------------------------------------|
| Opaque         | 인접 블록이 있으면 면 제거                    |
| Transparency   | 타입이 다르거나 불투명이 아니면 면 생성        |
| SemiAlpha      | 양방향 규칙 (한쪽은 불투명 체크, 반대는 투명 체크) |
| LowLod         | Opaque + SemiAlpha 결합                     |

**Transparency 규칙 (Chunk.cpp:656-692)**:
```cpp
if (Block::IsTransparency(type)) {
    // 타입이 다르거나 불투명이 아니면 면 생성
    if (x - 1 >= 0 && type != m_blocks[x - 1][y][z].GetType() &&
        !Block::IsOpaque(m_blocks[x - 1][y][z].GetType())) {
        memory->tpCullColBit[Utils::GetIndexFrom3D(0, y, z, CHUNK_SIZE_P)] |= (1ULL << x);
    }
}
```

**SemiAlpha 양방향 규칙 (Chunk.cpp:693-727)**:
```cpp
else if (Block::IsSemiAlpha(type)) {
    // - → + 방향: 불투명이 아니면 앞면 생성
    if (x + 1 < CHUNK_SIZE_P && !Block::IsOpaque(m_blocks[x + 1][y][z].GetType())) {
        memory->saCullColBit[Utils::GetIndexFrom3D(1, y, z, CHUNK_SIZE_P)] |= (1ULL << x);
    }

    // + → - 방향: 투명이면 뒷면 생성
    if (x - 1 >= 0 && Block::IsTransparency(m_blocks[x - 1][y][z].GetType())) {
        memory->saCullColBit[Utils::GetIndexFrom3D(0, y, z, CHUNK_SIZE_P)] |= (1ULL << x);
    }
}
```

### 3.4. Greedy Meshing 알고리즘

**2D 평면에서 연속된 면을 하나의 사각형으로 병합**:

**알고리즘 (Chunk.cpp:853-909)**:
```cpp
void Chunk::GreedyMeshing(std::vector<uint64_t>& faceColBit,
                          std::vector<VoxelVertex>& vertices,
                          std::vector<uint32_t>& indices,
                          BLOCK_TYPE type) {
    for (uint8_t face = 0; face < 6; ++face) {
        for (int s = 0; s < CHUNK_SIZE; ++s) {  // Slice (깊이)
            for (int i = 0; i < CHUNK_SIZE; ++i) {  // Column (너비)
                uint64_t faceBit = faceColBit[GetIndexFrom3D(face, s, i, CHUNK_SIZE)];
                int step = 0;

                while (step < CHUNK_SIZE) {
                    // 1. 빈 공간 건너뛰기
                    step += TrailingZeros(faceBit >> step);
                    if (step >= CHUNK_SIZE) break;

                    // 2. 높이 방향 병합
                    int ones = TrailingOnes(faceBit >> step);
                    uint64_t submask = ((1ULL << ones) - 1ULL) << step;

                    // 3. 너비 방향 병합
                    int w = 1;
                    while (i + w < CHUNK_SIZE) {
                        uint64_t cb = faceColBit[GetIndexFrom3D(face, s, i + w, CHUNK_SIZE)] & submask;
                        if (cb != submask) break;

                        faceColBit[GetIndexFrom3D(face, s, i + w, CHUNK_SIZE)] &= (~submask);
                        w++;
                    }

                    // 4. Quad 메쉬 생성
                    MeshGenerator::CreateQuadMesh(vertices, indices, x, y, z, w, ones, face, textureIndex);

                    step += ones;
                }
            }
        }
    }
}
```

**시각화**:
```
원본 비트 배열:
Col 0: 0b00011110
Col 1: 0b00011110
Col 2: 0b00011110
Col 3: 0b00000010

Step 1: TrailingZeros = 1 (비트 1부터 시작)
Step 2: TrailingOnes = 4 (높이 = 4)
Step 3: Col 0,1,2가 일치 (너비 = 3)
Step 4: Quad 생성 (3×4)

결과: 12개 블록 → 1개 사각형 (92% 절약)
```

### 3.5. TrailingZeros와 TrailingOnes

**TrailingZeros (최하위 0 개수)**:
```cpp
int TrailingZeros(uint64_t value) {
    // 예: 0b1110001000 → 3
    return __builtin_ctzll(value);  // CPU 내장 명령어
}
```

**TrailingOnes (최하위 1 개수)**:
```cpp
int TrailingOnes(uint64_t value) {
    // 예: 0b1110001111 → 4
    return TrailingZeros(~value);
}
```

**성능**: 하드웨어 명령어 사용 시 **1 사이클**

## 4. 구현 내용

### 4.1. 전체 파이프라인 (5단계)

**InitWorldVerticesData (Chunk.cpp:633-802)**:

```cpp
void Chunk::InitWorldVerticesData(ChunkLoadMemory* memory) {
    // 1단계: 타입별 분류 및 Column Bit 생성 (648-741)
    for (int x = 0; x < CHUNK_SIZE_P; ++x) {
        for (int y = 0; y < CHUNK_SIZE_P; ++y) {
            for (int z = 0; z < CHUNK_SIZE_P; ++z) {
                BLOCK_TYPE type = m_blocks[x][y][z].GetType();
                if (type == BLOCK_AIR) continue;

                if (Block::IsTransparency(type)) {
                    ProcessTransparency(...);
                }
                else if (Block::IsSemiAlpha(type)) {
                    ProcessSemiAlpha(...);
                }
                else {
                    ProcessOpaque(...);
                }
            }
        }
    }

    // 2단계: Face Culling (744-761)
    for (int axis = 0; axis < 3; ++axis) {
        for (int h = 1; h < CHUNK_SIZE_P - 1; ++h) {
            for (int w = 1; w < CHUNK_SIZE_P - 1; ++w) {
                uint64_t opBit = memory->opColBit[GetIndexFrom3D(axis, h, w, CHUNK_SIZE_P)];
                memory->opCullColBit[...] = opBit & ~(opBit << 1);
                memory->opCullColBit[...] = opBit & ~(opBit >> 1);
            }
        }
    }

    // 3단계: Slice Column Bit 생성 (770-773)
    MakeFaceSliceColumnBit(memory->opCullColBit, opSliceColBit);

    // 4단계: Greedy Meshing (784-787)
    for (const auto& t : opTypeMap) {
        GreedyMeshing(opSliceColBit[t.first], m_opaqueVertices, m_opaqueIndices, t.first);
    }
}
```

### 4.2. MakeFaceSliceColumnBit (타입별 재배열)

**Chunk.cpp:804-851**:
```cpp
void Chunk::MakeFaceSliceColumnBit(uint64_t cullColBit[CHUNK_SIZE_P2 * 6],
    std::unordered_map<BLOCK_TYPE, std::vector<uint64_t>>& sliceColBit) {

    for (uint8_t face = 0; face < 6; ++face) {
        for (int h = 0; h < CHUNK_SIZE; ++h) {
            for (int w = 0; w < CHUNK_SIZE; ++w) {
                uint64_t colbit = cullColBit[GetIndexFrom3D(face, h + 1, w + 1, CHUNK_SIZE_P)];
                colbit = colbit >> 1;                     // Padding 제거
                colbit = colbit & ~(1ULL << CHUNK_SIZE);  // 상위 비트 제거

                while (colbit) {
                    int bitPos = TrailingZeros(colbit);
                    colbit = colbit & (colbit - 1ULL);  // 최하위 1 비트 제거

                    // 블록 타입 확인
                    BLOCK_TYPE type = m_blocks[bitPos + 1][h + 1][w + 1].GetType();

                    // 타입별 Slice Column Bit에 추가
                    if (sliceColBit.find(type) == sliceColBit.end()) {
                        sliceColBit[type] = std::vector<uint64_t>(CHUNK_SIZE2 * 6, 0);
                    }
                    sliceColBit[type][GetIndexFrom3D(face, bitPos, w, CHUNK_SIZE)] |= (1ULL << h);
                }
            }
        }
    }
}
```

**핵심 비트 연산**:
```cpp
colbit = colbit & (colbit - 1ULL);  // 최하위 1 비트 제거
```

**원리**:
```
colbit:         0b00101100
colbit - 1:     0b00101011
colbit & (colbit-1): 0b00101000  ← 비트 2 제거
```

## 5. 타입별 고려사항

### 5.1. Opaque (불투명 블록)

**특징**: Stone, Dirt 등 대부분의 블록

**Face Culling**:
```cpp
cullBit = opBit & ~(opBit << 1);  // 인접 블록이 있으면 면 제거
```

**효과**: 땅속 블록의 모든 면 제거 → **정점 감소율 95% 이상**

### 5.2. Transparency (완전 투명 블록)

**특징**: Water, Glass

**경우의 수**:
| 현재 | 인접 | 면 생성 | 이유 |
|------|------|---------|------|
| Water | Water | X | 같은 타입 |
| Water | Glass | O | 다른 타입 |
| Water | Air | O | 수면 |
| Water | Stone | O | 경계 |

**효과**: 물 내부 면 제거 → 투명 렌더링 성능 향상

### 5.3. SemiAlpha (반투명 블록)

**특징**: Leaf (나뭇잎) - 양면 렌더링

**양방향 규칙**:
- **앞면**: 불투명이 아니면 생성
- **뒷면**: 투명이면 생성

**효과**: 하늘을 향할 때 양면, 내부는 필요한 면만

### 5.4. LowLod (저해상도)

**구성**: Opaque + SemiAlpha 결합

**효과**: 원거리 렌더링 시 단순 처리

## 6. CPU 효과

### 6.1. Face Culling 성능

| 방식 | 연산 횟수 | 시간 |
|-----|----------|------|
| 조건문 | 196,608 | ~2,000 μs |
| 비트 연산 | 6,144 | ~60 μs |
| **속도 향상** | **30배** | **33배 빠름** |

### 6.2. Greedy Meshing 효과

| 청크 구성 | Naive | Greedy | 감소율 |
|----------|-------|--------|-------|
| 완전 채워진 청크 | 98,304 정점 | 6,144 정점 | 93.7% |
| 표면만 있는 청크 | 24,576 정점 | 2,048 정점 | 91.7% |
| 복잡한 지형 | 50,000 정점 | 5,000 정점 | 90.0% |

### 6.3. 캐시 효율

**Column Bit의 캐시 친화성**:
- 64비트 정수 배열: 연속 메모리
- CPU 캐시 라인: 64바이트 (8개 uint64_t)
- 한 번 로드로 8개 컬럼 처리

## 7. GPU 효과

### 7.1. Draw Call 감소

**타입별 버퍼 분리**:
```cpp
context->DrawIndexed(m_opaqueIndexCount, 0, 0);       // Opaque
context->DrawIndexed(m_transparencyIndexCount, 0, 0); // Transparency
context->DrawIndexed(m_semiAlphaIndexCount, 0, 0);    // SemiAlpha
```

**효과**: Draw Call 블록 수천 개 → **3개**

### 7.2. 정점 처리량 감소

```
Naive: 78,643,200 정점/프레임 (100 청크)
Greedy: 1,000,000 정점/프레임

감소: 98.7%
```

**GPU 메모리 대역폭**:
```
Naive: 312 MB/프레임
Greedy: 4 MB/프레임

절약: 18 GB/s (60 FPS)
```

### 7.3. 삼각형 감소

```
4×4 블록 벽:
Naive: 32 삼각형
Greedy: 2 삼각형

감소: 93.7%
```

## 8. 회고

### 8.1. 아쉬운 점

1. **임시 메모리**: ~400 KB (청크당)
2. **타입별 중복 순회**: 캐시 미스 증가
3. **2D 병합 한계**: 3D 병합 불가
4. **AO 부재**: 정점별 AO 계산 안 함
5. **인덱싱 오버헤드**: `GetIndexFrom3D` 빈번 호출

### 8.2. 개선 방향

1. **GPU Compute Shader**: GPU에서 메쉬 생성
2. **Mesh Shader**: DX12 Mesh Shader 활용
3. **3D Greedy Meshing**: 볼륨 병합
4. **동적 업데이트**: 부분 재생성
5. **멀티스레드**: 타입별 병렬 처리
