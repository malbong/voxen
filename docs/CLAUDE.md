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

- Water를 정리하려고 해
- 아래는 내가 중요하다고 생각하는 것들이야
  - Mirror World 기반의 반사
  - Water Color 결정 방법 (투영 + 미러 + Water자체 +@기타 보정값)
  - Under Water에서의 Filter와 렌더링 순서 변경

A

- srcs/App.cpp (RenderMirrorWorld, RenderWaterPlane)
- srcs/Graphics.cpp (water 관련 PSO, mirror 관련 PSO)
- shaders/MirrorMasking.hlsl
- shaders/WaterPlanePS.hlsl
- shaders/WaterFilterPS.hlsl

B

- docs/gpu/Water/README.md
