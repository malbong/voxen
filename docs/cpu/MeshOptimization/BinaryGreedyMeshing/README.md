# BinaryGreedyMeshing

<table>
  <tr>
<td><img width="1916" height="1076" alt="Image" src="https://github.com/user-attachments/assets/adfdd990-455e-46af-b676-9d294e03f1dc" /></td>
    <td><img width="1915" height="1073" alt="Image" src="https://github.com/user-attachments/assets/7247997d-b1c3-4c82-abd4-a687542a22b5" /></td>
  </tr>
</table>

<table>
  <tr>
<td><img width="1918" height="1075" alt="Image" src="https://github.com/user-attachments/assets/71e14d63-ebb3-4e2f-b1a2-9abe0cb27b15" /></td>
    <td><img width="1917" height="1075" alt="Image" src="https://github.com/user-attachments/assets/eab5f23e-6bee-499e-a4b6-6e4ea775e561" /></td>
  </tr>
</table>

## 1. 개요

**비트 연산 기반 Binary Greedy Meshing 알고리즘**을 사용하여 복셀 블록 메쉬를 최적화하는 방법이다.
3D 블록 배열(32x32x32)을 **64비트 정수 배열(Column Bit)**로 변환한 후, **비트 시프트와 마스킹**으로 Face Culling, 타입별 분류, 사각형 병합을 수행한다.

메쉬의 개수가 확연히 줄어들게 되고, 할당하는 정점 데이터 메모리 마저 줄어들어 매우 효과적인 메싱 기법이다.

Chunk에 32x32x32 블록의 타입이 결정되고, 타입별로 따로 메쉬를 병합한다.
타입별로 진행하는 이유는 GPU에 적절한 TextureIndex를 넘겨야 하기 때문이다.

참고한 영상 레퍼런스: https://www.youtube.com/watch?v=qnGoGq7DWMc

## 2. 도입 동기

여러개의 블록을 메쉬 작업할 때 최적화하는 방법은 일반적으로 주변에 블록이 있다면 인접한(면이 맞닿는) 메쉬는 그리지 않는게 일반적이다.
하지만, 32x32 평면에서 모두 같은 블록일 때 큰 사각형 하나, 즉 삼각형 두개면 충분하지만 개별로 렌더링하는 경우 삼각형의 개수가 32x32 x 2가 되어 매우 비효율적인 렌더링 패스가 된다.
그래서, 주변의 블록을 검사하고 메쉬를 최적화하는 Greedy Meshing을 진행하게 되었다.

이 때, Greedy Meshing을 진행할 때 단순히 반복문을 이용하여 찾는 일은 병합하는 개수만큼 시간복잡도가 올라가게 되고 하나의 평면에 O(N^2)의 시간복잡도가 걸리게 된다.
찾아보니 비트 연산을 통한 병합 방법이 존재했고, 구현의 복잡도는 올라가나 효과적이라 판단하여 도입하게 되었다.

## 3. 핵심 아이디어

### 3.0. Face Culling도 비트 연산으로 충분히 가능함

```
a = 011101110010
위와 같은 형식으로 블록이 존재한다고 가정했을 때

왼쪽에서 오른쪽을 바라보는 경우 필요한 메쉬는 010001000010 이다. (실제로)
이를 구하기 위해서는 반복문이 아닌 비트 연산으로 충분히 구할 수 있다.

a를 한칸 오른쪽 쉬프트 -> 001110111001
이를 뒤집기 -> 110001000110
& 연산

011101110010
110001000110
=>
010001000010

cf. 쉬프트 했을 때 처음 0이 나오는 부분이 뒤집히면서 1이되고 &연산으로 활성화된다.
```

### 3.1. Column Bit 표현

이러한 효과적인 Face Culling을 진행하기 위해 바라보는 Face를 각 축으로 잡고 2D Column 비트를 구성한다.

**예시(x축에서 left -> right 방향: axis 0)**

<img width="459" height="443" alt="Image" src="https://github.com/user-attachments/assets/299925ff-1dd8-490e-924c-343a110f697a" />

```
x축 방향(- -> +)에서 (y=3, z=2)
m_blocks[0][3][2] = AIR      → 비트 0 = 0
m_blocks[1][3][2] = STONE    → 비트 1 = 1
m_blocks[2][3][2] = STONE    → 비트 2 = 1
m_blocks[3][3][2] = STONE    → 비트 3 = 1
m_blocks[4][3][2] = AIR      → 비트 4 = 0
...

↓ Column Bit

colBit[0][3][2] = 0b...00001110 (비트 1-3이 1)
                           ^^^
                        STONE 블록들
```

### 3.2. Face Column Bit를 Slice Face 기준 비트로 재구성

<img width="459" height="443" alt="Image" src="https://github.com/user-attachments/assets/4e41b034-12a1-47ee-82f7-553a09f3b4d7" />

현재는 왼쪽에서 오른쪽 바라보는 face 0 일 때, x 축에 나란한 비트열이 64비트 정수열에 저장되어 있지만
Face Culling을 마친 비트열은 다시 바라보는 방향에 맞는 Face Slice 평면 비트가 필요하다.

Face Slice에 맞게 Greedy Meshing을 진행하기 위해 비트열을 재구성한다.

<img width="231" height="272" alt="Image" src="https://github.com/user-attachments/assets/0918515c-f4dd-4594-ba21-939414b15573" />

<img width="277" height="255" alt="Image" src="https://github.com/user-attachments/assets/ba361fa1-710e-4102-8322-6bf2185d0a7d" />

### 3.3. Greedy Meshing 진행

**2D 평면에서 연속된 면을 하나의 사각형으로 병합**

알고리즘은 1의 연속된 개수를 찾고, 비트마스크를 만들어서 & 연산하여 검사 후 병합을 진행하게 된다.

**예시**:

```
원본 비트 배열:
Face Slice Col 0: 00011110
Face Slice Col 1: 00011110
Face Slice Col 2: 00011110
Face Slice Col 3: 00000010

Step 1: TrailingZeros = 1 (비트 1부터 시작)
Step 2: TrailingOnes = 4 (길이 = 4)
Step 3: Face Slice Col 0,1,2가 일치 (너비 = 3)
Step 4: Quad 생성 (3×4)

결과: 12개 블록 → 1개 사각형
```

### 3.4. 타입별 구분 필요

**블록 타입에 따라 서로 다른 Face Culling 규칙 적용 후 따로 Greedy Meshing**:

| 타입         | Face Culling 규칙                                  |
| ------------ | -------------------------------------------------- |
| Opaque       | 인접 블록이 있으면 면 제거                         |
| Transparency | 타입이 다르거나 불투명이 아니면 면 생성            |
| SemiAlpha    | 양방향 규칙 (한쪽은 불투명 체크, 반대는 투명 체크) |
| LowLod       | Opaque와 같음                                      |

## 4. 구현 내용

### 4.0. 타입맵 구성

청크에 실제로 존재하는 블록의 개수는 크게 다양하지 않다.

그래서 타입맵을 만들어 존재하는 개수만큼 FaceCulling 이후 Greedy Meshing을 진행하려고 타입맵을 구성했다.

```cpp
// 1. make axis column bit data
std::unordered_map<BLOCK_TYPE, bool> llTypeMap;
std::unordered_map<BLOCK_TYPE, bool> opTypeMap;
std::unordered_map<BLOCK_TYPE, bool> tpTypeMap;
std::unordered_map<BLOCK_TYPE, bool> saTypeMap;
```

### 4.1 로드 시 필요한 정적 메모리 사전 구성

청크를 로드할 때 필요한 메모리는 잠시 사용하고 버려질 데이터이긴 하나, 필요시에 동적으로 할당하거나 스택에 할당하기에는 너무 큰 메모리다.

예를 들어, 왼쪽에서 오른쪽을 바라보는 방향에 대해서 필요한 데이터는 `64bit x 34 x 34` 이고 Face별로 가지고 있어야하며 블록의 특성(투명, 불투명, 반투명)에 따라 가지고 있어야 한다. `64bit x 34 x 34 x 6 x 4`

그래서 ChunkManager에서 로드 시에 필요한 메모리를 미리 할당하고, 로드 시 메모리를 빌려주는 개념으로 사용한다.

cf. Face Slice Column Bit는 사전 구성하지 않고, 동적으로 메모리 할당하여 사용한다. (효과가 미비했었음)

```cpp
void Chunk::InitWorldVerticesData(ChunkLoadMemory* memory)
...

struct ChunkLoadMemory {
	uint64_t llColBit[Chunk::CHUNK_SIZE_P2 * 3];
	uint64_t opColBit[Chunk::CHUNK_SIZE_P2 * 3];

	uint64_t llCullColBit[Chunk::CHUNK_SIZE_P2 * 6];
	uint64_t opCullColBit[Chunk::CHUNK_SIZE_P2 * 6];
	uint64_t tpCullColBit[Chunk::CHUNK_SIZE_P2 * 6];
	uint64_t saCullColBit[Chunk::CHUNK_SIZE_P2 * 6];

    // ...
    // 더 많은 노이즈 데이터와 로드 시 필요한 메모리 데이터
}
```

### 4.2 블록 특성 별 Face Culling 규칙 및 구현

월드에 존재하는 블록의 특성과 규칙은 다음과 같다.

```
ll: 먼거리 블록 -> 단순히 Column 비트 구성 후 비트 연산으로 Face Culling
op: 불투명 블록 -> 단순히 Column 비트 구성 후 비트 연산으로 Face Culling
tp: 투명 블록  -> 블록 구성 요소에 따라 곧바로 Face Culling
sa: 반투명 블록 -> 블록 구성 요소에 따라 곧바로 Face Culling
```

lowlod와 opaque 특성의 블록 들은 Column 비트를 구성 후 비트 쉬프트 연산으로 Face Culling을 한번에 진행한다.
이와 달리 투명 블록(tp)과 반투명 블록(sa)은 주변 Block의 특성에 따라 곧바로 Face Culling을 진행하게 된다.
cf. 위와 같은 규칙은 임의로 정한 것이 아닌, 실제 Minecraft에서 몇가지 실험결과로 얻어지게 되었다.

```
ts: 주변 블록의 타입이 다르고, 그 블록이 불투명한 블록이 아닌 경우 Face가 존재한다고 판단
sa: 타입과 무관하게, 한쪽(- -> + 방향)은 불투명한 물체가 아닌 경우 Face 존재, 다른 한쪽(+ -> - 방향)은 투명한 경우 Face 존재
```

코드

```cpp
// void Chunk::InitWorldVerticesData(ChunkLoadMemory* memory)

for (int x = 0; x < CHUNK_SIZE_P; ++x) {
	for (int y = 0; y < CHUNK_SIZE_P; ++y) {
		for (int z = 0; z < CHUNK_SIZE_P; ++z) {
			BLOCK_TYPE type = m_blocks[x][y][z].GetType();

			if (type == BLOCK_TYPE::BLOCK_AIR)
				continue;

            // 투명한 물체인 경우
			if (Block::IsTransparency(type)) {
				tpTypeMap[type] = true;

				// 주변이 타입이 다르고, 불투명 물체가 아닌 경우 페이스 존재
				if (x - 1 >= 0 && type != m_blocks[x - 1][y][z].GetType() &&
					!Block::IsOpaque(m_blocks[x - 1][y][z].GetType())) {
					memory->tpCullColBit[Utils::GetIndexFrom3D(0, y, z, CHUNK_SIZE_P)] |=
						(1ULL << x);
				}
                ...
                // y, z 방향
			}
            // 반투명 물체인 경우 (나뭇잎)
			else if (Block::IsSemiAlpha(type)) {
				saTypeMap[type] = true;

				// - -> + : 불투명이 아니면 페이스 존재 -> 같은 타입을 고려하지 않음
				if (x + 1 < CHUNK_SIZE_P && !Block::IsOpaque(m_blocks[x + 1][y][z].GetType())) {
					memory->saCullColBit[Utils::GetIndexFrom3D(1, y, z, CHUNK_SIZE_P)] |=
						(1ULL << x);
				}

				// + -> - : 투명일 때만 페이스 존재 -> 같은 타입을 고려하지 않음
				if (x - 1 >= 0 && Block::IsTransparency(m_blocks[x - 1][y][z].GetType())) {
					memory->saCullColBit[Utils::GetIndexFrom3D(0, y, z, CHUNK_SIZE_P)] |=
						(1ULL << x);
				}

                // ll 타입에는 Column Bit 등록
				llTypeMap[type] = true;
				memory->llColBit[Utils::GetIndexFrom3D(0, y, z, CHUNK_SIZE_P)] |= (1ULL << x);
                ...
			}
            // 불투명 물체
			else {
				opTypeMap[type] = true;
				memory->opColBit[Utils::GetIndexFrom3D(0, y, z, CHUNK_SIZE_P)] |= (1ULL << x);
                ...

				llTypeMap[type] = true;
				memory->llColBit[Utils::GetIndexFrom3D(0, y, z, CHUNK_SIZE_P)] |= (1ULL << x);
                ...
			}
		}
	}
}


// 3. face cull: lowlod & opaque
for (int axis = 0; axis < 3; ++axis) {
	for (int h = 1; h < CHUNK_SIZE_P - 1; ++h) {
		for (int w = 1; w < CHUNK_SIZE_P - 1; ++w) {
			uint64_t llBit = memory->llColBit[Utils::GetIndexFrom3D(axis, h, w, CHUNK_SIZE_P)];
			memory->llCullColBit[Utils::GetIndexFrom3D(axis * 2 + 0, h, w, CHUNK_SIZE_P)] =
				llBit & ~(llBit << 1);
			memory->llCullColBit[Utils::GetIndexFrom3D(axis * 2 + 1, h, w, CHUNK_SIZE_P)] =
				llBit & ~(llBit >> 1);

			uint64_t opBit = memory->opColBit[Utils::GetIndexFrom3D(axis, h, w, CHUNK_SIZE_P)];
			memory->opCullColBit[Utils::GetIndexFrom3D(axis * 2 + 0, h, w, CHUNK_SIZE_P)] =
				opBit & ~(opBit << 1);
			memory->opCullColBit[Utils::GetIndexFrom3D(axis * 2 + 1, h, w, CHUNK_SIZE_P)] =
				opBit & ~(opBit >> 1);
		}
	}
}
```

### 4.3. MakeFaceSliceColumnBit (타입별 재배열 및 Face Slice Column 구성)

축에 나란한 비트열을 바라보는 방향에 대한 Slice 평면의 비트열로 재구성한다.

이 때, 이 데이터를 가지고 Greedy Meshing을 진행하기 때문에 블록의 타입별로 구성한다.

추가적으로 최하위비트(`bitPos`)가 평면의 인덱스가 되고, `w`,`h` 는 Slice 평면에 맞게 적절히 변환된다.

```cpp
void Chunk::MakeFaceSliceColumnBit(uint64_t cullColBit[CHUNK_SIZE_P2 * 6],
    std::unordered_map<BLOCK_TYPE, std::vector<uint64_t>>& sliceColBit) {

    for (uint8_t face = 0; face < 6; ++face) {
        for (int h = 0; h < CHUNK_SIZE; ++h) {
            for (int w = 0; w < CHUNK_SIZE; ++w) {
                uint64_t colbit = cullColBit[GetIndexFrom3D(face, h + 1, w + 1, CHUNK_SIZE_P)];
                colbit = colbit >> 1;                     // Padding 제거
                colbit = colbit & ~(1ULL << CHUNK_SIZE);  // Padding 제거

                while (colbit) {
                    // bitPos가 바라보는 방향의 Slice 면임
                    int bitPos = TrailingZeros(colbit); // 1110001000 -> trailing zero : 3
                    colbit = colbit & (colbit - 1ULL);  // 1110000000 -> 최하위 1 비트 제거

                    // 실제 블록 타입 찾기
                    BLOCK_TYPE type = BLOCK_TYPE::BLOCK_AIR;
                    if (face <= DIR::RIGHT) { // left right
                        type = m_blocks[bitPos + 1][h + 1][w + 1].GetType();
                    }
                    else if (face <= DIR::TOP) { // bottom top
                        type = m_blocks[w + 1][bitPos + 1][h + 1].GetType();
                    }
                    else { //(face < 6) // front back
                        type = m_blocks[w + 1][h + 1][bitPos + 1].GetType();
                    }

                    if (sliceColBit.find(type) == sliceColBit.end()) {
                        sliceColBit[type] = std::vector<uint64_t>(Chunk::CHUNK_SIZE2 * 6, 0);
                    }

                    sliceColBit[type][Utils::GetIndexFrom3D(face, bitPos, w, CHUNK_SIZE)] |= (1ULL << h);
                }
            }
        }
    }
}
```

### 4.4 BinaryGreedyMeshing

비트열을 가지고 병합을 시작한다.

비트열 연산자가 많아 코드가 조금 복잡하다.

```cpp
void Chunk::GreedyMeshing(std::vector<uint64_t>& faceColBit, std::vector<VoxelVertex>& vertices,
	std::vector<uint32_t>& indices, BLOCK_TYPE type)
{
	// face 0, 1 : left,right
	// face 2, 3 : bottom, top
	// face 4, 5 : front,back
	for (uint8_t face = 0; face < 6; ++face) {
		TEXTURE_INDEX textureIndex = (TEXTURE_INDEX)Block::GetBlockTextureIndex(type, face);

		for (int s = 0; s < CHUNK_SIZE; ++s) { // face slice
			for (int i = 0; i < CHUNK_SIZE; ++i) { // step index

				uint64_t faceBit = faceColBit[Utils::GetIndexFrom3D(face, s, i, CHUNK_SIZE)];

				int step = 0;
				while (step < CHUNK_SIZE) {						   // 111100011100
					step += Utils::TrailingZeros(faceBit >> step); // 1111000111|00| -> 2
					if (step >= CHUNK_SIZE)
						break;

                    // 서브마스크 구성
					int ones = Utils::TrailingOnes((faceBit >> step));	// 1111000|111|00 -> 3
					uint64_t submask = ((1ULL << ones) - 1ULL) << step; // 111 << 2 -> 11100

                    // 병합 탐색 시작
					int w = 1;
					while (i + w < CHUNK_SIZE) {
						uint64_t cb =
							faceColBit[Utils::GetIndexFrom3D(face, s, i + w, CHUNK_SIZE)] & submask;

						if (cb != submask)
							break;

                        // 서브마스크와 동일한 경우 해당 비트열을 제거하고 병합의 크기를 증가시킴 (w++)
						faceColBit[Utils::GetIndexFrom3D(face, s, i + w, CHUNK_SIZE)] &= (~submask);
						w++;
					}

                    // face 별로 x,y,z, length, mergeCount를 구성
					if (face == DIR::LEFT)
						MeshGenerator::CreateQuadMesh(
							vertices, indices, s, step, i, w, ones, face, textureIndex);
					else if (face == DIR::RIGHT)
						MeshGenerator::CreateQuadMesh(
							vertices, indices, s + 1, step, i, w, ones, face, textureIndex);
					else if (face == DIR::BOTTOM)
						MeshGenerator::CreateQuadMesh(
							vertices, indices, i, s, step, w, ones, face, textureIndex);
					else if (face == DIR::TOP)
						MeshGenerator::CreateQuadMesh(
							vertices, indices, i, s + 1, step, w, ones, face, textureIndex);
					else if (face == DIR::FRONT)
						MeshGenerator::CreateQuadMesh(
							vertices, indices, i, step, s, w, ones, face, textureIndex);
					else // face == DIR::BACK
						MeshGenerator::CreateQuadMesh(
							vertices, indices, i, step, s + 1, w, ones, face, textureIndex);

					step += ones;
				}
			}
		}
	}
}

```

## 5. 효과

### 프로젝트 초기에 진행했던 단순한 벤치마크 (노이즈 거의 없음, 블록타입 같음, 싱글쓰레드 환경, 인텔내장GPU)

<img width="900" height="500" alt="Image" src="https://github.com/user-attachments/assets/1693cb6f-1330-4cfb-bc2e-0a639a52fdd5" />
<img width="1280" height="79" alt="Image" src="https://github.com/user-attachments/assets/95e61b96-5e16-4bb0-86ed-195737b9d584" />

Greedy Meshing 전 (인접한 블록면만 제거한 경우)

- 평균 로딩 속도: 1280ms
- 사용 메모리: 1600MB
- 평균 프레임: 15~ FPS

<img width="900" height="500" alt="Image" src="https://github.com/user-attachments/assets/f5d39f4d-4d50-4c04-9ba6-5f5d7ac3ef84" />
<img width="1280" height="77" alt="Image" src="https://github.com/user-attachments/assets/2ea81d07-accb-47bd-9a01-e80ab6c9567e" />

Greedy Meshing 후

- 평균 평균 약 530ms
- 사용 메모리: 400MB
- 평균 프레임: 50~ FPS

## 6. 문제점 및 해결

### 문제 1

초기화에 필요한 많은 메모리 공간

- 해결: 메모리를 초기화 때 동적으로 구성하는게 아닌, 미리 할당해놓고 초기화 시 빌려서 사용

### 문제 2

블록 특성에 따른 메싱 방법의 상이

- 해결: Minecraft에서 실험하여 규칙을 얻고 블록에 특성을 부여하고 그에 따른 Face Culling을 따로 진행함

### 문제 3

다양한 블록에 대한 유연성 부족

- 해결: 꽃이나 풀 같은 경우 Block이 아닌 Instance로 구분하여 따로 렌더링

## 7. 회고

- 동영상 하나로 모든 것을 구현해야했던 챕터
  - 많은 힌트가 존재했지만 실제로 구현하는데는 힘든 챕터였다.
- 비용은 많이 줄였지만, 유지보수나 유연성이 낮아 다른 로직을 추가하는데 어려움을 느꼈다.
  - PBR 진행하면서 Height Mapping을 추가하고 싶었지만 GreedyMeshing으로 인해 삼각형의 크기가 달라, TS의 방법이 떠오르지 않았다.
  - 블록에 따라 상세도를 높히는 경우 16x16 메싱된 삼각형에서는 괜찮겠지만, 1x1에서도 TS되어 GPU 부하가 심해질 것 같았다.
  - 그래서 HeightMapping은 포기하게 되었다.
  - 동적으로 메싱을 하는 경우 HeightMapping은 어떻게 적용하는지 모르겠다.
