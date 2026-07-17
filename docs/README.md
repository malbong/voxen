## 개요

- 프로젝트를 진행하면서 활용한 기능들을 정리한 문서입니다.
- 복습용 기능 정리 문서입니다.
- Claude Code를 활용하여 README.md 초안을 작성하고 수정하였습니다.

<br />
<br />

## **GPU 관점**

| 목차                 | 내용                                           | 문서 경로                                              |
| -------------------- | ---------------------------------------------- | ------------------------------------------------------ |
| Cascade Shadow Map   | 그림자 구현을 위한 쉐도우맵                    | [gpu/CascadeShadowMap/](gpu/CascadeShadowMap/)         |
| DeferredShading_MSAA | G-Buffer 구성 후 Edge Stencil MSAA로 쉐이딩    | [gpu/DeferredShading_MSAA/](gpu/DeferredShading_MSAA/) |
| Lighting PBR         | 텍스쳐(N,M,R)와 HDR로 semi unreal PBR 구현     | [gpu/Lighting_PBR/](gpu/Lighting_PBR/)                 |
| SSAO                 | 간접광 음영을 위한 SSAO (MSAA 처리)            | [gpu/SSAO/](gpu/SSAO/)                                 |
| MSAA Issue 해결      | Extrapolation문제와 ddx/ddy mipmap문제 해결    | [gpu/MSAA_Issues_Seams/](gpu/MSAA_Issues_Seams/)       |
| PostEffect           | 렌더 결과를 Bloom과 선형톤매핑                 | [gpu/PostEffect/](gpu/PostEffect/)                     |
| Water Reflection     | Plane 반사와 Depth를 이용한 투영 및 물속필터   | [gpu/WaterReflection/](gpu/WaterReflection/)           |
| Cloud                | 노이즈를 활용한 메쉬 구성 및 반투명 렌더링     | [gpu/Cloud/](gpu/Cloud/)                               |
| SkyBox               | 큐브맵 텍스쳐 없이 PS를 이용한 실시간 동적하늘 | [gpu/SkyBox/](gpu/SkyBox/)                             |
| Fog                  | Depth Buffer를 활용한 안개 구현                | [gpu/Fog/](gpu/Fog/)                                   |
| GrassLeafColor       | 부드러운 잔디색을 위한 노이즈와 Color Map 활용 | [gpu/GrassLeafColor/](gpu/GrassLeafColor/)             |
| FrustumCulling       | CPU에서 ViewFrustum에 대한 Culling 진행        | [gpu/FrustumCulling/](gpu/FrustumCulling/)             |

---

## **CPU 관점**

### 1. Chunk Management

| 목차                | 내용                                  | 문서 경로                                                                            |
| ------------------- | ------------------------------------- | ------------------------------------------------------------------------------------ |
| Chunk Structure     | 블록의 집합인 청크의 구조 및 데이터   | [cpu/ChunkManagement/ChunkStructure/](cpu/ChunkManagement/ChunkStructure/)           |
| Chunk Manager       | 청크 관리의 중심인 Update 리스트 관리 | [cpu/ChunkManagement/ChunkManager/]([cpu/ChunkManagement/ChunkManager/])             |
| Chunk Load Unload   | 실질적인 청크 로드와 언로드의 흐름    | [cpu/ChunkManagement/ChunkLoadUnload/](cpu/ChunkManagement/ChunkLoadUnload/)         |
| Chunk Patch         | 로드된 청크를 변경하는 Patch          | [cpu/ChunkManagement/ChunkPatch/](cpu/ChunkManagement/ChunkPatch/)                   |
| AsyncMultithreading | 멀티쓰레드 관리                       | [cpu/ChunkManagement/AsyncMultithreading/](cpu/ChunkManagement/AsyncMultithreading/) |

### 2. Mesh Optimization

| 목차                | 내용                                      | 문서 경로                                                                              |
| ------------------- | ----------------------------------------- | -------------------------------------------------------------------------------------- |
| BinaryBlockInfo     | 메모리를 아끼기 위한 비트단위 데이터 구성 | [cpu/MeshOptimization/BinaryBlockInfo/](cpu/MeshOptimization/BinaryBlockInfo/)         |
| BinaryGreedyMeshing | 비트연산기반 Greedy Meshing 알고리즘      | [cpu/MeshOptimization/BinaryGreedyMeshing/](cpu/MeshOptimization/BinaryGreedyMeshing/) |

### 3. World Generation

| 목차     | 내용                                         | 문서 경로                                                      |
| -------- | -------------------------------------------- | -------------------------------------------------------------- |
| Terrain  | 노이즈를 활용한 절차적 지형 생성             | [cpu/WorldGeneration/Terrain/](cpu/WorldGeneration/Terrain/)   |
| Biome    | 노이즈 매개변수를 이용한 12 바이옴 결정 방식 | [cpu/WorldGeneration/Biome/](cpu/WorldGeneration/Biome/)       |
| Block    | 다양한 블록 타입 결정 방식                   | [cpu/WorldGeneration/Block/](cpu/WorldGeneration/Block/)       |
| Tree     | 트리 타입 및 형태 결정 그리고 Chunk Patch    | [cpu/WorldGeneration/Tree/](cpu/WorldGeneration/Tree/)         |
| Instance | 인스턴스(스프라이트) 타입 및 Chunk Patch     | [cpu/WorldGeneration/Instance/](cpu/WorldGeneration/Instance/) |

---
