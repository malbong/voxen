## 개요

- 프로젝트를 진행하면서 활용한 기능들을 정리한 문서입니다.
- 복습용 기능 정리 문서입니다.
- Claude Code를 활용하여 README.md를 작성했습니다.

<br />

## 목록

### **GPU 관점**

| 목차                 | 내용                                           | 문서 경로                        |
| -------------------- | ---------------------------------------------- | -------------------------------- |
| Cascade Shadow Map   | 그림자 구현을 위한 쉐도우맵                    | `docs/gpu/CascadeShadowMap/`     |
| Cloud                | 노이즈를 활용한 메쉬 구성 및 반투명 렌더링     | `docs/gpu/Cloud/`                |
| DeferredShading_MSAA | G-Buffer 구성 후 Edge Stencil MSAA로 쉐이딩    | `docs/gpu/DeferredShading_MSAA/` |
| Fog                  | Depth Buffer를 활용한 안개 구현                | `docs/gpu/Fog/`                  |
| FrustumCulling       | CPU에서 ViewFrustum에 대한 Culling 진행        | `docs/gpu/Frustum/`              |
| GrassLeafColor       | 부드러운 잔디색을 위한 노이즈와 Color Map 활용 | `docs/gpu/GrassLeafColor/`       |
| Lighting             | 텍스쳐(N,M,R)와 HDR로 semi unreal PBR 구현     | `docs/gpu/Lighting/`             |
| MSAA Issue 해결      | Extrapolation문제와 ddx/ddy mipmap문제 해결    | `docs/gpu/MSAA_issues/`          |
| PostEffect           | 렌더 결과를 Bloom과 선형톤매핑                 | `docs/gpu/PostEffect/`           |
| SkyBox               | 큐브맵 텍스쳐 없이 PS를 이용한 실시간 동적하늘 | `docs/gpu/SkyBox/`               |
| SSAO                 | 간접광 음영을 위한 SSAO (MSAA 처리)            | `docs/gpu/SSAO/`                 |
| Water                | Plane 반사와 Depth를 이용한 투영 및 물속필터   | `docs/gpu/Water/`                |

---

### **CPU 관점**

| 목차                 | 내용                                              | 문서 경로                                        |
| -------------------- | ------------------------------------------------- | ------------------------------------------------ |
| 1. Chunk Management  |                                                   |                                                  |
| Chunk Life Cycle     | 청크의 생성 -> 패치 -> 소멸의 내부연산            | `docs/cpu/ChunkManagement/ChunkLifecycle/`       |
| Chunk Structure      | 블록의 집합 청크와 청크의 집합 청크매니저         | `docs/cpu/ChunkManagement/ChunkStructure/`       |
| Chunk Update         | 매프레임 청크 리스트 관리                         | `docs/cpu/ChunkManagement/ChunkUpdate/`          |
| -------------------- |                                                   |                                                  |
| 2. Mesh Optimization |                                                   |                                                  |
| BinaryBlockInfo      | 메모리를 아끼기 위한 비트단위 데이터 구성         | `docs/cpu/MeshOptimization/BinaryBlockInfo/`     |
| BinaryGreedyMeshing  | 비트연산기반 Greedy Meshing 알고리즘              | `docs/cpu/MeshOptimization/BinaryGreedyMeshing/` |
| -------------------- |                                                   |                                                  |
| 3. World Generation  |                                                   |                                                  |
| Biome                | 노이즈 매개변수를 이용한 13 바이옴 결정 방식      | `docs/cpu/Biome/`                                |
| BlockType            | 3단계 선택 로직으로 다양한 블록 타입 결정 방식    | `docs/cpu/BlockType/`                            |
| Terrain              | 노이즈를 활용한 절차적 지형 생성                  | `docs/cpu/Terrain/`                              |
| Tree                 | 8 트리 타입 및 형태 결정 그리고 Chunk Patch 전파  | `docs/cpu/Tree/`                                 |
| WorldMap             | Biome과 지형에 대한 2D 월드맵 텍스쳐 구성 및 렌더 | `docs/cpu/WorldMap/`                             |

---
