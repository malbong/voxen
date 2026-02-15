# 작성 템플릿

```
## 1. 개요

- 이 기능이 무엇인지
- 엔진/프로젝트 전체에서 어떤 역할인지

## 2. 도입 동기

- 왜 이 기능이 필요했는지
- 기존 방식의 한계 / 목표 성능 or 비주얼

## 3. 핵심 아이디어

- 설계에서 가장 중요한 포인트
- 알고리즘

## 4. 구현 내용

- 실제 구현한 방식
- 사용한 API, 자료구조, 셰이더 단계 등

## 5. 문제점 & 해결

- 구현 중 겪은 문제 및 해결 방법
- 트레이드오프 유무 및 방식

## 6. 결과

-

## 7. 회고

- 아쉬운 점
- 다음에 개선하고 싶은 방향
```

# 작성 제한 사항

- 작성 내용은 Target README.md 에만 할 것
  - A: 참고할 소스 코드 및 쉐이더 내용
    - voxen/headers, voxen/srcs, voxen/shaders 참고할 것
    - release/ 혹은 voxen/assets 같은 바이너리 파일을 읽지 않을 것
  - B: 출력할 README.md 파일경로
- 무조건 작성 템플릿을 그대로 따를 필요는 없음
  - 문서 작성용이기 때문에 적절한 템플릿 타이틀이 존재하면 그것을 사용할 것

# 작성 목적

- 해당 프로젝트는 주니어 개발자가 만든 voxel 렌더러 정리용
- 개인 복습
- 포트폴리오용 문서

# 작업 지시

Comment

- MSAA_Issues 를 작성할거야
- Voxel을 렌더링하면서 GreedyMeshing이나 Data 메모리를 아끼다보니 다양한 문제가 발생했어
- 문제점은 아래와 같아
  - Frac연산과 Texture 좌표가 작은 구간에 크게 변동되어 Mipmap이 높은 레벨을 설정하게 됨
  - Extrapolation으로 Bleeding이 발생

1. Bleeding 문제 해결

- Linear, Centroid, Sample Interpolation에 대한 설명과 Sample Interpolation을 사용한 이유에 대해서 중점적으로 서술해야해
  - Linear를 사용한 경우 Extrapolation됨
  - Centroid도 적절하지만 하나의 블록 내부에서 Sample의 개수가 4개여도 같은 Interpolation값을 사용하게 되기에 PointSS를 사용하지 못함
  - 그 결과 Sample Interpolation 방식을 사용했음

2. mipmap 문제 해결

- Sample Interpolation과 Texture 좌표 frac연산을 하지 않음

A

- srcs/App.cpp, shaders/BasicVS.hlsl, shaders/BasicPS.hlsl

B

- docs/gpu/MSAA_Issues/README.md
