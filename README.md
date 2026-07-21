# 프로젝트 결과

<img width="720" height="406" alt="Image" src="https://github.com/user-attachments/assets/ce15170f-e977-43e8-8329-aba829ecec3f" />

---

<a href="https://www.youtube.com/watch?v=3bn0ek6wgbY" target="_blank">
▶️ Click to watch on YouTube
<img width="1916" height="1075" alt="Image" src="https://github.com/user-attachments/assets/f6a5388f-af15-49f5-92f2-c425b04b84fc" />

<img width="1911" height="1074" alt="Image" src="https://github.com/user-attachments/assets/632a2bec-fc12-4d73-b7d0-017d89baf698" />
</a>

<br />

# 프로젝트 개요

- **설명**
  - 마인크래프트 모작의 오픈 월드 렌더링 DX11, C++ 프로젝트
  - MSAA를 적용한 디퍼드 셰이딩, 캐스케이드 섀도우 매핑, SSAO, PBR 라이팅, 멀티스레드 청크 관리 기반의 실시간 월드 생성 등 구현

- **개발 인원**
  - 개인 프로젝트
- **개발 기간**
  - `1차`: 2024.04 ~ 2025.01 (기본 렌더링 구현)
  - `2차`: 2025.07 ~ 2026.01 (청크 패치 및 보완점 수정)

<br />

# 빌드 및 실행

이 프로젝트는 Visual Studio 2022를 사용하여 빌드합니다.

- **빌드 방법:**
  - 직접 빌드를 진행하지 않아도 `release/` 폴더에 `voxen.exe`가 존재합니다
  - Visual Studio 2022에서 `voxen.sln`을 열고 release x64 빌드
  - 빌드 시 `release/` 폴더가 생성되며, 실행에 필요한 파일이 자동으로 배치됩니다

- **실행 방법:**
  - `release\voxen.exe` 실행

- **`release/` 폴더 구조:**

  ```
  release/
  ├── voxen.exe        # 실행 파일
  ├── assets/          # 텍스쳐, 모델 등 리소스
  ├── shaders/         # HLSL 쉐이더 파일
  └── imgui.ini        # GUI 설정
  ```

<br />

# 흐름 도식화

### GPU 파이프라인 도식화

```
App::Render()
│
├── 1. Shadow Map Pass
├── 2. Deferred Rendering Pass
│     ├── G-Buffer Fill
│     ├── MSAA Edge Masking
│     ├── SSAO
│     └── Deferred Shading (PBR)
├── 3. ConvertToMSAA
├── 4. Forward Rendering Pass (수면 위/아래 분기)
├── 5. Post Effect (Water Filter + Bloom + Tone Mapping)
└── 6. UI (Biome Map)
```

<img width="958" height="608" alt="Image" src="https://github.com/user-attachments/assets/fe17fe61-469d-4067-bc44-457f0c83ae7c" />

### 버퍼 전체 흐름도 (상세)

<img width="1616" height="938" alt="Image" src="https://github.com/user-attachments/assets/a32d8711-9312-41c8-9d89-482dd3fcf04e" />

### CPU Chunk Update 도식화

```
ChunkManager::Update()
│
├── 1. UpdateChunkList()        — 카메라 이동 시 로드/언로드 대상 결정
├── 2. UpdateLoadChunkList()    — 비동기 로드 (월드 생성 + 메싱)
├── 3. UpdateUnloadChunkList()  — 범위 밖 청크 해제
├── 4. UpdatePatchChunkMap()    — 비동기 패치 (경계 블록 수정)
├── 5. UpdateRenderChunkList()  — 프러스텀 컬링 → 렌더 리스트 구성
├── 6. UpdateInstanceInfoList() — 인스턴스 버퍼 갱신
└── 7. UpdateChunkConstant()    — 청크 상수 버퍼 갱신
```

<br />

# 프레임 캡쳐 - RenderDoc

<img width="1743" height="986" alt="Image" src="https://github.com/user-attachments/assets/0b31d302-43f1-4f75-8214-69b3a426585b" />

| 패스                | 시간 (ms) | 비율  |
| ------------------- | --------- | ----- |
| Fill G-Buffer       | 4.73      | 40.1% |
| Planar Mirror World | 2.27      | 19.2% |
| Cascade Shadow Map  | 1.78      | 15.1% |
| SSAO                | 1.05      | 8.9%  |
| Water Plane         | 0.88      | 7.5%  |
| Deferred Shading    | 0.54      | 4.6%  |
| Mask MSAA Edge      | 0.22      | 1.8%  |
| Post Effects        | 0.15      | 1.2%  |
| Fog Filter          | 0.10      | 0.8%  |
| Convert To MSAA     | 0.03      | 0.3%  |
| Cloud               | 0.02      | 0.1%  |
| Skybox              | 0.02      | 0.1%  |
| ImGUI               | 0.01      | 0.1%  |

### Fill G-Buffer (40.1%)

- 5개의 렌더 타겟
- Binary Greedy Meshing로 vertex 수 최소화
- Frustum Culling으로 불필요한 DrawCall 제거

### Planar Mirror World + Water Plane (26.7%)

- Water 표면을 그리기 위한 비용이 큼
- SSR을 사용하지 않고 반사 물체를 그대로 렌더링하여 비용이 큼

### SSAO / Shading (16.4%)

- Mask MSAA Edge를 통한 2Pass 분리
- 엣지 픽셀에 대해서만 따로 4x 샘플링 연산하여 비용 절감 (프레임 전반에 대해서는 미비한 효과)

<br />

# 시스템 구성요소 및 문서

개별 기능에 대한 상세 문서는 `docs/`에 위치합니다.

- `docs/` - 문서에 대한 개요
- `docs/gpu/` - GPU 측 렌더링 기술
- `docs/cpu/` - CPU 측 시스템 (청크 관리, 월드 생성, 메시 최적화)

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
| Chunk Manager       | 청크 관리의 중심인 Update 리스트 관리 | [docs/cpu/ChunkManagement/ChunkManager/](docs/cpu/ChunkManagement/ChunkManager/)             |
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
