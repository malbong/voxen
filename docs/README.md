## 개요

- 프로젝트를 진행하면서 활용한 기능들을 정리한 문서입니다.
- 복습용 기능 정리 문서입니다.
- Claude Code를 활용하여 README.md를 작성했습니다.

<br />
<br />

## **GPU 관점**

| 목차                 | 내용                                           | 문서 경로                                                        |
| -------------------- | ---------------------------------------------- | ---------------------------------------------------------------- |
| Cascade Shadow Map   | 그림자 구현을 위한 쉐도우맵                    | [docs/gpu/CascadeShadowMap/](docs/gpu/CascadeShadowMap/)         |
| DeferredShading_MSAA | G-Buffer 구성 후 Edge Stencil MSAA로 쉐이딩    | [docs/gpu/DeferredShading_MSAA/](docs/gpu/DeferredShading_MSAA/) |
| Lighting PBR         | 텍스쳐(N,M,R)와 HDR로 semi unreal PBR 구현     | [docs/gpu/Lighting_PBR/](docs/gpu/Lighting_PBR/)                 |
| SSAO                 | 간접광 음영을 위한 SSAO (MSAA 처리)            | [docs/gpu/SSAO/](docs/gpu/SSAO/)                                 |
| MSAA Issue 해결      | Extrapolation문제와 ddx/ddy mipmap문제 해결    | [docs/gpu/MSAA_Issues_Seams/](docs/gpu/MSAA_Issues_Seams/)       |
| PostEffect           | 렌더 결과를 Bloom과 선형톤매핑                 | [docs/gpu/PostEffect/](docs/gpu/PostEffect/)                     |
| Water Reflection     | Plane 반사와 Depth를 이용한 투영 및 물속필터   | [docs/gpu/WaterReflection/](docs/gpu/WaterReflection/)           |
| Cloud                | 노이즈를 활용한 메쉬 구성 및 반투명 렌더링     | [docs/gpu/Cloud/](docs/gpu/Cloud/)                               |
| SkyBox               | 큐브맵 텍스쳐 없이 PS를 이용한 실시간 동적하늘 | [docs/gpu/SkyBox/](docs/gpu/SkyBox/)                             |
| Fog                  | Depth Buffer를 활용한 안개 구현                | [docs/gpu/Fog/](docs/gpu/Fog/)                                   |
| GrassLeafColor       | 부드러운 잔디색을 위한 노이즈와 Color Map 활용 | [docs/gpu/GrassLeafColor/](docs/gpu/GrassLeafColor/)             |
| FrustumCulling       | CPU에서 ViewFrustum에 대한 Culling 진행        | [docs/gpu/FrustumCulling/](docs/gpu/FrustumCulling/)             |

---

## **CPU 관점**

### 1. Chunk Management

| 목차                | 내용                                  | 문서 경로                                                                                      |
| ------------------- | ------------------------------------- | ---------------------------------------------------------------------------------------------- |
| Chunk Structure     | 블록의 집합인 청크의 구조 및 데이터   | [docs/cpu/ChunkManagement/ChunkStructure/](docs/cpu/ChunkManagement/ChunkStructure/)           |
| Chunk Manager       | 청크 관리의 중심인 Update 리스트 관리 | [docs/cpu/ChunkManagement/ChunkManager/]([docs/cpu/ChunkManagement/ChunkManager/])             |
| Chunk Load Unload   | 실질적인 청크 로드와 언로드의 흐름    | [docs/cpu/ChunkManagement/ChunkLoadUnload/](docs/cpu/ChunkManagement/ChunkLoadUnload/)         |
| Chunk Patch         | 로드된 청크를 변경하는 Patch          | [docs/cpu/ChunkManagement/ChunkPatch/](docs/cpu/ChunkManagement/ChunkPatch/)                   |
| AsyncMultithreading | 멀티쓰레드 관리                       | [docs/cpu/ChunkManagement/AsyncMultithreading/](docs/cpu/ChunkManagement/AsyncMultithreading/) |

### 2. Mesh Optimization

| 목차                | 내용                                      | 문서 경로                                                                                        |
| ------------------- | ----------------------------------------- | ------------------------------------------------------------------------------------------------ |
| BinaryBlockInfo     | 메모리를 아끼기 위한 비트단위 데이터 구성 | [docs/cpu/MeshOptimization/BinaryBlockInfo/](docs/cpu/MeshOptimization/BinaryBlockInfo/)         |
| BinaryGreedyMeshing | 비트연산기반 Greedy Meshing 알고리즘      | [docs/cpu/MeshOptimization/BinaryGreedyMeshing/](docs/cpu/MeshOptimization/BinaryGreedyMeshing/) |

### 3. World Generation

| 목차     | 내용                                         | 문서 경로                                                                |
| -------- | -------------------------------------------- | ------------------------------------------------------------------------ |
| Terrain  | 노이즈를 활용한 절차적 지형 생성             | [docs/cpu/WorldGeneration/Terrain/](docs/cpu/WorldGeneration/Terrain/)   |
| Biome    | 노이즈 매개변수를 이용한 12 바이옴 결정 방식 | [docs/cpu/WorldGeneration/Biome/](docs/cpu/WorldGeneration/Biome/)       |
| Block    | 다양한 블록 타입 결정 방식                   | [docs/cpu/WorldGeneration/Block/](docs/cpu/WorldGeneration/Block/)       |
| Tree     | 트리 타입 및 형태 결정 그리고 Chunk Patch    | [docs/cpu/WorldGeneration/Tree/](docs/cpu/WorldGeneration/Tree/)         |
| Instance | 인스턴스(스프라이트) 타입 및 Chunk Patch     | [docs/cpu/WorldGeneration/Instance/](docs/cpu/WorldGeneration/Instance/) |

---
