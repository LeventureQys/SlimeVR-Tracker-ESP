/**
 * MIT 开源真实人手蒙皮模型及关节驱动。
 *
 * 输入：手腕相对零位四元数 Hamilton [w,x,y,z]，五指预计算 bend/sway（deg）。
 * 输出：驱动 WebXR generic-hand 的 25 个标准关节和蒙皮，并更新与关节严格对齐的骨骼覆盖层。
 * 坐标：算法 X 沿实物四指、Y 为另一掌内轴、Z 穿掌；模型 Y 沿四指、X 横掌、Z 穿掌。
 * 显示层使用 C=Rz(-90°) 做 q_H=C⊗q_A⊗C⁻¹，将算法 +X 映射到模型 -Y、算法 +Y
 * 映射到模型 +X；两个掌内轴的正方向同时反转，用于匹配实物观察到的顺时针方向。
 * 状态：保存 GLB 原始绑定姿态、每指关节链和最近一帧；模型异步加载完成后自动应用待显示姿态。
 *
 * 资产：@webxr-input-profiles/assets 1.0.15 generic-hand/left.glb，MIT License。
 * 限制：表面和关节位置用于工程可视化而非医疗诊断；网页只按已有 bend/sway 驱动模型。
 */

import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';

const FINGER_ORDER = ['thumb', 'index', 'middle', 'ring', 'little'];
const MODEL_PREFIX = { thumb: 'thumb', index: 'index-finger', middle: 'middle-finger', ring: 'ring-finger', little: 'pinky-finger' };
// 算法坐标 A 到左手模型显示坐标 H 的固定旋转：A.X→-H.Y、A.Y→H.X、A.Z→H.Z。
// 不能只交换四元数 x/y 分量；那不是完整基变换，并会破坏旋转轴和符号的一致性。
const ALGORITHM_TO_HAND = new THREE.Quaternion().setFromAxisAngle(
  new THREE.Vector3(0, 0, 1),
  -Math.PI / 2,
);
const HAND_TO_ALGORITHM = ALGORITHM_TO_HAND.clone().invert();
// 各指“完全握拳”在传感器整体相对转角上的实测初值。网页只据此计算完成度，数值读数和
// CSV 保留最高 180°，不再截断到 90°。后续有更多佩戴者数据时可移入配置文件标定。
const FULL_FIST_BEND_DEG = { thumb: 85, index: 155, middle: 160, ring: 120, little: 145 };
const HAND_ASSET = new URL('../assets/generic-hand-left.glb', import.meta.url).href;

/**
 * 将算法 Hamilton 四元数转换成手模型的 Three.js 四元数。
 * 返回新对象，便于坐标测试独立验证 X→-Y、Y→X、Z→Z，不修改调用者数据。
 */
export function algorithmQuaternionToHand(wristQuaternionWxyz) {
  const [w, x, y, z] = wristQuaternionWxyz;
  const algorithmRotation = new THREE.Quaternion(x, y, z, w).normalize();
  return ALGORITHM_TO_HAND.clone()
    .multiply(algorithmRotation)
    .multiply(HAND_TO_ALGORITHM)
    .normalize();
}

export class AnatomicalHandModel {
  /** 创建空根节点并异步加载本地 GLB；调用方可立即提交姿态，加载后会自动补应用。 */
  constructor(scene) {
    this.scene = scene;
    this.root = new THREE.Group();
    this.root.name = 'algorithm-wrist-root';
    scene.add(this.root);

    this.modelScene = null;
    this.joints = new Map();
    this.rest = new Map();
    this.chains = new Map();
    this.skinMeshes = [];
    this.boneOverlay = new THREE.Group();
    this.boneOverlay.name = 'anatomical-bone-overlay';
    scene.add(this.boneOverlay);
    this.boneConnections = [];
    this.jointMarkers = [];
    this.skinVisible = true;
    this._loading = false;
    this._loadId = 0;
    this.pendingPose = {
      wrist: [1, 0, 0, 0],
      fingers: Object.fromEntries(FINGER_ORDER.map((name) => [name, { bendDeg: 0, swayDeg: 0 }])),
    };
    this.ready = this.#loadModel();
  }

  #disposeCurrentModel() {
    if (this.modelScene) {
      this.root.remove(this.modelScene);
      const disposed = new Set();
      this.modelScene.traverse((object) => {
        if (object.geometry && typeof object.geometry.dispose === 'function') {
          object.geometry.dispose();
        }
        const materials = Array.isArray(object.material) ? object.material : [object.material];
        materials.forEach((material) => {
          if (material && typeof material.dispose === 'function' && !disposed.has(material)) {
            disposed.add(material);
            material.dispose();
          }
        });
      });
      this.modelScene = null;
    }
    [...this.boneOverlay.children].forEach((child) => {
      if (child.geometry && typeof child.geometry.dispose === 'function') child.geometry.dispose();
      if (child.material && typeof child.material.dispose === 'function') child.material.dispose();
      this.boneOverlay.remove(child);
    });
    this.joints.clear();
    this.rest.clear();
    this.chains.clear();
    this.skinMeshes = [];
    this.boneConnections = [];
    this.jointMarkers = [];
    this.palmNormal = null;
  }

  /** 加载真实蒙皮、记录标准关节绑定姿态、建立解剖链，并把手自动居中到约 7 个场景单位。 */
  async #loadModel() {
    const loadId = ++this._loadId;
    this._loading = true;
    this.#disposeCurrentModel();
    try {
      const loader = new GLTFLoader();
      const gltf = await loader.loadAsync(HAND_ASSET);
      if (loadId !== this._loadId) {
        gltf.scene.traverse((object) => {
          if (object.geometry) object.geometry.dispose();
        });
        return this;
      }
      this.modelScene = gltf.scene;
      this.modelScene.name = 'webxr-generic-left-hand';

      const skinMaterial = new THREE.MeshPhysicalMaterial({
        color: 0xd9bca4,
        roughness: 0.58,
        metalness: 0.0,
        transparent: true,
        opacity: 0.43,
        transmission: 0.04,
        clearcoat: 0.08,
        depthWrite: false,
        side: THREE.DoubleSide,
      });

      this.modelScene.traverse((object) => {
        if (object.name) this.joints.set(object.name, object);
        if (object.isSkinnedMesh) {
          object.material = skinMaterial;
          object.frustumCulled = false;
          this.skinMeshes.push(object);
        }
      });

      this.#requiredJointNames().forEach((name) => {
        const joint = this.joints.get(name);
        if (!joint) throw new Error(`真实手模型缺少标准关节：${name}`);
        this.rest.set(name, {
          position: joint.position.clone(),
          quaternion: joint.quaternion.clone(),
          scale: joint.scale.clone(),
        });
      });

      // 原资产手掌位于 YZ 平面且指尖朝 -Y；固定旋转后让掌面朝相机、指尖朝屏幕 +Y。
      const faceCamera = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), -Math.PI / 2);
      const fingersUp = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 0, 1), Math.PI);
      this.modelScene.quaternion.copy(fingersUp).multiply(faceCamera);
      this.root.add(this.modelScene);
      this.modelScene.updateMatrixWorld(true);

      const initialBounds = new THREE.Box3().setFromObject(this.modelScene);
      const initialSize = initialBounds.getSize(new THREE.Vector3());
      const displayScale = 7.05 / Math.max(initialSize.x, initialSize.y, initialSize.z);
      this.modelScene.scale.setScalar(displayScale);
      this.modelScene.updateMatrixWorld(true);
      const scaledBounds = new THREE.Box3().setFromObject(this.modelScene);
      const scaledCenter = scaledBounds.getCenter(new THREE.Vector3());
      this.modelScene.position.sub(scaledCenter);
      this.modelScene.position.y += 0.35;
      this.modelScene.updateMatrixWorld(true);

      this.#buildFingerChains();
      this.#buildBoneOverlay();
      this.setSkinVisible(this.skinVisible);
      this.applyPose(this.pendingPose.wrist, this.pendingPose.fingers);
      return this;
    } finally {
      this._loading = false;
    }
  }

  /** 返回本项目驱动所需的 WebXR 标准关节名。 */
  #requiredJointNames() {
    const names = ['wrist'];
    FINGER_ORDER.forEach((finger) => {
      const prefix = MODEL_PREFIX[finger];
      names.push(`${prefix}-metacarpal`, `${prefix}-phalanx-proximal`);
      if (finger !== 'thumb') names.push(`${prefix}-phalanx-intermediate`);
      names.push(`${prefix}-phalanx-distal`, `${prefix}-tip`);
    });
    return names;
  }

  /**
   * 建立从掌骨到指尖的真实关节链。
   * 长指在 MCP（proximal）开始弯曲；拇指从 metacarpal 开始，以表达腕掌关节的较大活动度。
   */
  #buildFingerChains() {
    FINGER_ORDER.forEach((finger) => {
      const prefix = MODEL_PREFIX[finger];
      const names = finger === 'thumb'
        ? [`${prefix}-metacarpal`, `${prefix}-phalanx-proximal`, `${prefix}-phalanx-distal`, `${prefix}-tip`]
        : [`${prefix}-metacarpal`, `${prefix}-phalanx-proximal`, `${prefix}-phalanx-intermediate`, `${prefix}-phalanx-distal`, `${prefix}-tip`];
      const pivotIndex = finger === 'thumb' ? 0 : 1;
      const direction = this.rest.get(names[pivotIndex + 1]).position.clone().sub(this.rest.get(names[pivotIndex]).position).normalize();
      this.chains.set(finger, { names, pivotIndex, direction });
    });

    const indexMeta = this.rest.get('index-finger-metacarpal').position;
    const littleMeta = this.rest.get('pinky-finger-metacarpal').position;
    const middleMeta = this.rest.get('middle-finger-metacarpal').position;
    const middleProximal = this.rest.get('middle-finger-phalanx-proximal').position;
    const acrossPalm = littleMeta.clone().sub(indexMeta).normalize();
    const alongPalm = middleProximal.clone().sub(middleMeta).normalize();
    this.palmNormal = acrossPalm.clone().cross(alongPalm).normalize();
  }

  /** 根据真实关节连接创建覆盖骨骼；圆柱端点与蒙皮关节位置完全一致，不再人工猜长度。 */
  #buildBoneOverlay() {
    const boneMaterial = new THREE.MeshPhysicalMaterial({ color: 0xf0dfbd, roughness: 0.62, clearcoat: 0.12 });
    const jointMaterial = new THREE.MeshStandardMaterial({ color: 0xcdb28a, roughness: 0.72 });
    const connectionKeys = new Set();

    this.chains.forEach((chain) => {
      for (let index = 0; index < chain.names.length - 1; index += 1) {
        const startName = chain.names[index];
        const endName = chain.names[index + 1];
        const key = `${startName}|${endName}`;
        if (connectionKeys.has(key)) continue;
        connectionKeys.add(key);
        const cylinder = new THREE.Mesh(new THREE.CylinderGeometry(0.075, 0.06, 1, 14), boneMaterial);
        cylinder.renderOrder = 2;
        this.boneOverlay.add(cylinder);
        this.boneConnections.push({ startName, endName, mesh: cylinder });
      }
    });

    this.#requiredJointNames().forEach((name) => {
      const marker = new THREE.Mesh(new THREE.SphereGeometry(name === 'wrist' ? 0.13 : 0.095, 16, 12), jointMaterial);
      marker.renderOrder = 2;
      this.boneOverlay.add(marker);
      this.jointMarkers.push({ name, mesh: marker });
    });
    this.#updateBoneOverlay();
  }

  /** 恢复所有关节的 GLB 原始绑定姿态，确保每次应用帧都由零位独立计算。 */
  #restoreBindPose() {
    this.rest.forEach((state, name) => {
      const joint = this.joints.get(name);
      joint.position.copy(state.position);
      joint.quaternion.copy(state.quaternion);
      joint.scale.copy(state.scale);
    });
  }

  /**
   * 按单指关节链应用 bend/sway。
   * sway 绕掌面法向，bend 绕“指向 × 掌面法向”的屈曲轴；每级位置由上一真实关节和原始骨长递推。
   */
  #poseFinger(finger, bendDeg, swayDeg) {
    const chain = this.chains.get(finger);
    if (!chain || !this.palmNormal) return;
    // bendDeg 表示整根手指从伸直到握拳的传感器角度/完成度，而不是三个解剖关节角的总和。
    // 旧实现把 90° 按权重拆成总共 90°，所以只能形成浅弯。现在按各指实测满握拳角度计算完成度，
    // 再让长指 MCP/PIP/DIP 达到约 75°/100°/55°；拇指三个活动关节使用较小的独立范围。
    const bendCompletion = THREE.MathUtils.clamp(bendDeg / FULL_FIST_BEND_DEG[finger], 0, 1);
    const sway = THREE.MathUtils.degToRad(THREE.MathUtils.clamp(swayDeg, -30, 30));
    const jointBendDeg = finger === 'thumb'
      ? [38, 52, 62]
      : [75, 100, 55];
    // 左手 GLB 局部屈曲轴仍朝手背，显示层取负号，使算法握拳为正时模型向掌心弯。
    const jointBends = jointBendDeg.map((angle) => -THREE.MathUtils.degToRad(angle * bendCompletion));
    const longFingerBendAxis = chain.direction.clone().cross(this.palmNormal).normalize();
    // 左手拇指的主要“弯曲”同样是对掌：从掌外侧转向掌心。交换显示用 bend/sway 轴；算法数值不变。
    const bendAxis = finger === 'thumb' ? this.palmNormal.clone() : longFingerBendAxis;
    const swayAxis = finger === 'thumb' ? longFingerBendAxis : this.palmNormal;
    const swayRotation = new THREE.Quaternion().setFromAxisAngle(swayAxis, sway);
    const firstBendAxis = bendAxis.clone().applyQuaternion(swayRotation);
    const firstBend = new THREE.Quaternion().setFromAxisAngle(firstBendAxis, jointBends[0]);
    let accumulated = firstBend.multiply(swayRotation);

    const pivotName = chain.names[chain.pivotIndex];
    const pivot = this.joints.get(pivotName);
    pivot.quaternion.copy(this.rest.get(pivotName).quaternion).premultiply(accumulated);
    let previousPosition = this.rest.get(pivotName).position.clone();

    for (let index = chain.pivotIndex + 1; index < chain.names.length; index += 1) {
      const name = chain.names[index];
      const previousName = chain.names[index - 1];
      const restVector = this.rest.get(name).position.clone().sub(this.rest.get(previousName).position);
      const joint = this.joints.get(name);
      joint.position.copy(previousPosition).add(restVector.applyQuaternion(accumulated));
      joint.quaternion.copy(this.rest.get(name).quaternion).premultiply(accumulated);
      previousPosition = joint.position.clone();

      const weightIndex = index - chain.pivotIndex;
      if (weightIndex < jointBends.length) {
        const currentAxis = bendAxis.clone().applyQuaternion(accumulated);
        const nextBend = new THREE.Quaternion().setFromAxisAngle(currentAxis, jointBends[weightIndex]);
        accumulated.premultiply(nextBend);
      }
    }
  }

  /** 应用一帧算法结果；模型尚未加载时只缓存，避免页面启动竞态。 */
  applyPose(wristQuaternionWxyz, fingerAngles) {
    this.pendingPose = { wrist: wristQuaternionWxyz, fingers: fingerAngles };
    if (!this.modelScene || this.chains.size === 0) return;
    this.#restoreBindPose();
    FINGER_ORDER.forEach((finger) => {
      const angles = fingerAngles[finger] ?? { bendDeg: 0, swayDeg: 0 };
      this.#poseFinger(finger, Number(angles.bendDeg) || 0, Number(angles.swayDeg) || 0);
    });

    // 同一个物理旋转在模型坐标中的表达必须做共轭基变换 C*q*C^-1。
    // 例如算法绕四指方向 +X 的旋转会成为模型绕四指方向 -Y；轴线不变、显示方向反转。
    this.root.quaternion.copy(algorithmQuaternionToHand(wristQuaternionWxyz));
    this.modelScene.updateMatrixWorld(true);
    this.#updateBoneOverlay();
  }

  /** 根据每个标准关节的当前世界位置更新骨骼圆柱和关节球。 */
  #updateBoneOverlay() {
    const yAxis = new THREE.Vector3(0, 1, 0);
    this.boneConnections.forEach(({ startName, endName, mesh }) => {
      const start = this.joints.get(startName).getWorldPosition(new THREE.Vector3());
      const end = this.joints.get(endName).getWorldPosition(new THREE.Vector3());
      const direction = end.clone().sub(start);
      const length = direction.length();
      mesh.position.copy(start).add(end).multiplyScalar(0.5);
      mesh.quaternion.setFromUnitVectors(yAxis, direction.normalize());
      mesh.scale.set(1, length, 1);
    });
    this.jointMarkers.forEach(({ name, mesh }) => {
      mesh.position.copy(this.joints.get(name).getWorldPosition(new THREE.Vector3()));
    });
  }

  /** 恢复单位手腕姿态和五指张开零位。 */
  resetPose() {
    this.applyPose([1, 0, 0, 0], Object.fromEntries(FINGER_ORDER.map((name) => [name, { bendDeg: 0, swayDeg: 0 }])));
  }

  /** 显示/隐藏真实蒙皮；骨骼覆盖层始终可见，便于核对关节链。 */
  setSkinVisible(visible) {
    this.skinVisible = visible;
    this.skinMeshes.forEach((mesh) => { mesh.visible = visible; });
  }
}

export { FINGER_ORDER };
