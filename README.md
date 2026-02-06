## 개요

- **Minecraft를 기반으로한 Voxel 오픈월드 렌더링 프로젝트**
- **개발 인원**
  - 개인 프로젝트
- **개발 기간**
  - `1차`: 2024.04 ~ 2025.01 (기본 렌더링 구현)
  - `2차`: 2025.07 ~ 2026.01 (청크 패치 및 보완점 수정)

<br />

## 결과

-

<br />

## 프로젝트 내용

-

<br />

## 파이프라인 도식화

-

<br />

## 시스템 구성요소 및 문서

- **GPU**
  - **Cascade ShadowMap**
  - **Deferred Shading For MSAA**
  - **SSAO**
  - **Lighting**
  - **Water**
  - **Frustum Culling**
  - **Shaders**
    - **Fog**
    - **Skybox**
    - **Cloud**
    - **Grass Leaf Color**

- **CPU**
  - **Chunk Management**
    - **Chunk**
    - **Update**
    - **Load / Unload**
    - **Patch**
    - **Picking**
  - **Mesh Optimization**
    - **Binary Block Info**
    - **Binary Greedy Meshing**
  - **World Generation**
    - **Terrain**
    - **Biome**
    - **Tree**
    - **BlockType**
    - **Worldmap**

<br />

## 보완 필요 부분

- MirrorPlane Reflection → SSR
- SSAO 쉐이더 비용 절감 필요
- 코드 (static header singleton) 일관성 필요
- GPU Resource 소멸 관리
