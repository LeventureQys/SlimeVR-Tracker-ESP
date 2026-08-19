/**
 * 灵巧手网页入口与界面编排。
 *
 * 输入：主控姿态帧。
 * 输出：Three.js 人手、五指 bend/sway 读数和选中手指曲线。
 * 坐标：Hamilton [w,x,y,z] 先重排为 Three.js [x,y,z,w]，再由 hand-model.js 完成显示坐标变换。
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { AnatomicalHandModel, FINGER_ORDER } from './hand-model.js';

const FINGER_LABELS = {
  thumb: '拇指',
  index: '食指',
  middle: '中指',
  ring: '无名指',
  little: '小指',
};

/** 食指、无名指实物向左时，算法 sway 正值却把模型和条形推向右侧。仅这两指左右对调，弯曲不改。 */
const LATERAL_DISPLAY_SIGN = { thumb: 1, index: -1, middle: 1, ring: -1, little: 1 };

function displaySwayDeg(finger, swayDeg) {
  return Number(swayDeg) * (LATERAL_DISPLAY_SIGN[finger] ?? 1);
}

const elements = Object.fromEntries([
  'scene', 'statusDot', 'sceneStatus', 'fpsValue', 'resetViewButton', 'toggleSkinButton',
  'toggleGridButton', 'frameCounter', 'summaryTime', 'sampleInterval', 'maxBend', 'maxSway',
  'fingerRows', 'angleChart', 'chartTitle',
  'serialPortSelect', 'serialBaudSelect', 'refreshPortsButton', 'serialConnectButton',
  'serialDisconnectButton', 'serialCalibrateButton', 'serialStatus', 'serialBadge',
].map((id) => [id, document.getElementById(id)]));

const scene = new THREE.Scene();
scene.fog = new THREE.FogExp2(0x080d15, 0.035);

const camera = new THREE.PerspectiveCamera(34, 1, 0.1, 100);
const defaultCameraPosition = new THREE.Vector3(-4.2, 0.7, 12.2);
const defaultControlTarget = new THREE.Vector3(0, 0.3, 0);
camera.position.copy(defaultCameraPosition);

const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.12;
elements.scene.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.07;
controls.target.copy(defaultControlTarget);
controls.minDistance = 7.2;
controls.maxDistance = 18;

scene.add(new THREE.HemisphereLight(0xc8e1ff, 0x17202b, 2.0));
const keyLight = new THREE.DirectionalLight(0xfff2dd, 4.4);
keyLight.position.set(-4.5, 3.5, 7.5);
scene.add(keyLight);
const rimLight = new THREE.DirectionalLight(0x79aaff, 3.0);
rimLight.position.set(5.5, -1.5, 4.5);
scene.add(rimLight);
const fillLight = new THREE.PointLight(0x49d9d0, 1.7, 12);
fillLight.position.set(-3, -3, 3);
scene.add(fillLight);

const grid = new THREE.GridHelper(10, 20, 0x29435b, 0x172537);
grid.rotation.x = Math.PI / 2;
grid.position.z = -1.2;
grid.material.transparent = true;
grid.material.opacity = 0.34;
scene.add(grid);

const handModel = new AnatomicalHandModel(scene);
handModel.ready.then(() => {
  if (!liveApply) elements.sceneStatus.textContent = '真实手模型就绪';
}).catch((error) => {
  elements.sceneStatus.textContent = '手模型加载失败';
  elements.serialStatus.textContent = `真实手模型加载失败：${error.message}`;
});
let selectedFinger = 'index';
let activeFrames = [];
let lastFrameTime = performance.now();
let fpsAccumulator = 0;
let fpsFrames = 0;
let lastFpsUpdate = performance.now();

/** 为五根手指建立可点击读数行；条形范围 bend=0~90°、sway=-30~30°。 */
function buildFingerRows() {
  elements.fingerRows.replaceChildren();
  FINGER_ORDER.forEach((name) => {
    const row = document.createElement('button');
    row.type = 'button';
    row.className = `finger-row${name === selectedFinger ? ' selected' : ''}`;
    row.dataset.finger = name;
    row.innerHTML = `
      <span class="finger-name">${FINGER_LABELS[name]}</span>
      <span class="angle-track"><span class="angle-fill bend" data-role="bend-fill"></span></span>
      <span class="angle-value" data-role="bend-value">0.0</span>
      <span class="angle-track"><span class="angle-fill sway" data-role="sway-fill"></span></span>
      <span class="angle-value" data-role="sway-value">0.0</span>`;
    row.addEventListener('click', () => {
      selectedFinger = name;
      elements.chartTitle.textContent = `${FINGER_LABELS[name]}曲线`;
      elements.fingerRows.querySelectorAll('.finger-row').forEach((item) => {
        item.classList.toggle('selected', item.dataset.finger === name);
      });
      drawChart(activeFrames, Math.max(0, activeFrames.length - 1));
    });
    elements.fingerRows.appendChild(row);
  });
}

/** 将一帧姿态映射到模型和读数控件。 */
function renderFrame(frame, index, frames) {
  if (!frame) return;
  activeFrames = frames;
  const angles = {};
  let maxBend = 0;
  let maxSway = 0;
  FINGER_ORDER.forEach((name) => {
    const finger = frame.fingers[name];
    const bendDeg = Number(finger?.bend_deg ?? 0);
    const swayDeg = displaySwayDeg(name, finger?.sway_deg ?? 0);
    angles[name] = { bendDeg, swayDeg };
    maxBend = Math.max(maxBend, bendDeg);
    maxSway = Math.max(maxSway, Math.abs(swayDeg));

    const row = elements.fingerRows.querySelector(`[data-finger="${name}"]`);
    row.querySelector('[data-role="bend-fill"]').style.width = `${THREE.MathUtils.clamp(bendDeg / 90 * 100, 0, 100)}%`;
    const swayFill = row.querySelector('[data-role="sway-fill"]');
    const swayPercent = THREE.MathUtils.clamp(swayDeg / 30 * 50, -50, 50);
    swayFill.style.width = `${Math.abs(swayPercent)}%`;
    swayFill.style.left = swayPercent < 0 ? `${50 + swayPercent}%` : '50%';
    row.querySelector('[data-role="bend-value"]').textContent = bendDeg.toFixed(1);
    row.querySelector('[data-role="sway-value"]').textContent = `${swayDeg >= 0 ? '+' : ''}${swayDeg.toFixed(1)}`;
  });

  handModel.applyPose(frame.wrist_quaternion_wxyz, angles);
  const relativeTime = frame.time_s - frames[0].time_s;
  const intervalMs = index > 0 ? (frame.time_s - frames[index - 1].time_s) * 1000 : 0;
  elements.summaryTime.textContent = `${relativeTime.toFixed(3)} s`;
  elements.sampleInterval.textContent = index > 0 ? `${intervalMs.toFixed(2)} ms` : '-- ms';
  elements.frameCounter.textContent = `${index + 1} / ${frames.length}`;
  elements.maxBend.textContent = `${maxBend.toFixed(1)}°`;
  elements.maxSway.textContent = `${maxSway.toFixed(1)}°`;
  drawChart(frames, index);
}

/** 绘制选中手指的近期角度，并用当前帧竖线标识位置。 */
function drawChart(frames, currentIndex) {
  const canvas = elements.angleChart;
  const context = canvas.getContext('2d');
  const width = canvas.width;
  const height = canvas.height;
  context.clearRect(0, 0, width, height);
  context.fillStyle = '#091019';
  context.fillRect(0, 0, width, height);

  const padding = { left: 36, right: 12, top: 15, bottom: 24 };
  const plotWidth = width - padding.left - padding.right;
  const plotHeight = height - padding.top - padding.bottom;
  const yMin = -30;
  const yMax = 90;
  const yFor = (value) => padding.top + (yMax - value) / (yMax - yMin) * plotHeight;

  context.strokeStyle = 'rgba(159,183,210,.13)';
  context.fillStyle = '#647487';
  context.font = '18px ui-monospace, monospace';
  context.textAlign = 'right';
  [-30, 0, 30, 60, 90].forEach((tick) => {
    const y = yFor(tick);
    context.beginPath();
    context.moveTo(padding.left, y);
    context.lineTo(width - padding.right, y);
    context.stroke();
    context.fillText(String(tick), padding.left - 7, y + 5);
  });

  if (frames.length < 2) return;
  const step = Math.max(1, Math.ceil(frames.length / Math.floor(plotWidth)));
  const drawSeries = (field, color) => {
    context.strokeStyle = color;
    context.lineWidth = 2.5;
    context.beginPath();
    for (let index = 0; index < frames.length; index += step) {
      const x = padding.left + index / (frames.length - 1) * plotWidth;
      const raw = Number(frames[index].fingers[selectedFinger]?.[field] ?? 0);
      const value = field === 'sway_deg' ? displaySwayDeg(selectedFinger, raw) : raw;
      const y = yFor(THREE.MathUtils.clamp(value, yMin, yMax));
      if (index === 0) context.moveTo(x, y);
      else context.lineTo(x, y);
    }
    context.stroke();
  };
  drawSeries('bend_deg', '#49d9d0');
  drawSeries('sway_deg', '#ffbd66');

  const markerX = padding.left + currentIndex / (frames.length - 1) * plotWidth;
  context.strokeStyle = 'rgba(255,255,255,.7)';
  context.lineWidth = 1;
  context.beginPath();
  context.moveTo(markerX, padding.top);
  context.lineTo(markerX, padding.top + plotHeight);
  context.stroke();
}

/** 响应视口尺寸和设备像素比，保持模型与图表区域清晰。 */
function resizeRenderer() {
  const { clientWidth, clientHeight } = elements.scene;
  renderer.setSize(clientWidth, clientHeight, false);
  camera.aspect = clientWidth / Math.max(1, clientHeight);
  camera.updateProjectionMatrix();
}

function resetView() {
  camera.position.copy(defaultCameraPosition);
  controls.target.copy(defaultControlTarget);
  controls.update();
}

elements.resetViewButton.addEventListener('click', resetView);
elements.toggleSkinButton.addEventListener('click', () => {
  const visible = elements.toggleSkinButton.getAttribute('aria-pressed') !== 'true';
  elements.toggleSkinButton.setAttribute('aria-pressed', String(visible));
  handModel.setSkinVisible(visible);
});
elements.toggleGridButton.addEventListener('click', () => {
  grid.visible = !grid.visible;
  elements.toggleGridButton.setAttribute('aria-pressed', String(grid.visible));
});

const LIVE_CHART_MAX = 400;
let liveApply = false;
let liveFrames = [];
let liveEventSource = null;

function setSerialUi(connected) {
  elements.serialConnectButton.disabled = connected;
  elements.serialDisconnectButton.disabled = !connected;
  elements.serialCalibrateButton.disabled = !connected;
  elements.serialPortSelect.disabled = connected;
  elements.serialBaudSelect.disabled = connected;
}

function updateSerialStatus(status) {
  if (!status) return;
  const phase = status.phase || 'idle';
  elements.serialBadge.textContent = {
    idle: '未连接',
    connecting: '连接中',
    calibrating: '标定中',
    live: '处理实时',
    error: '错误',
  }[phase] || phase;
  elements.serialStatus.textContent =
    `${status.message || ''} · 样本 ${status.sample_count ?? 0} · 输出帧 ${status.output_frame_count ?? 0}` +
    (status.port ? ` · ${status.port}@${status.baud}` : '');
  elements.statusDot.classList.toggle('ready', phase === 'live' || phase === 'calibrating');
  if (liveApply) {
    elements.serialCalibrateButton.disabled = phase === 'calibrating' || phase === 'error';
  }
  if (!liveApply) return;
  if (phase === 'live') {
    elements.sceneStatus.textContent = '主控姿态控制';
  } else if (phase === 'calibrating') {
    elements.sceneStatus.textContent = '串口标定中';
  }
}

function applyLiveFrame(frame) {
  if (!liveApply || !frame) return;
  liveFrames.push(frame);
  if (liveFrames.length > LIVE_CHART_MAX) liveFrames.shift();
  renderFrame(frame, liveFrames.length - 1, liveFrames);
}

async function refreshSerialPorts({ autoConnect = false } = {}) {
  try {
    const response = await fetch('/api/serial/ports');
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
    const select = elements.serialPortSelect;
    const previous = select.value;
    select.replaceChildren();
    const ports = payload.ports || [];
    if (!ports.length) {
      const option = document.createElement('option');
      option.value = '';
      option.textContent = '未发现串口';
      select.appendChild(option);
    } else {
      ports.forEach((port) => {
        const option = document.createElement('option');
        option.value = port.device;
        option.textContent = `${port.device} · ${port.description || ''}`.trim();
        select.appendChild(option);
      });
      if ([...select.options].some((item) => item.value === previous)) select.value = previous;
    }
    if (autoConnect && !liveApply && ports.length === 1 && ports[0].device) {
      select.value = ports[0].device;
      await connectSerial();
    }
  } catch (error) {
    elements.serialStatus.textContent = `刷新端口失败：${error.message}`;
  }
}

function stopLiveStream() {
  if (liveEventSource) {
    liveEventSource.close();
    liveEventSource = null;
  }
}

function startLiveStream() {
  stopLiveStream();
  liveEventSource = new EventSource('/api/live/stream');
  liveEventSource.onmessage = (event) => {
    try {
      const payload = JSON.parse(event.data);
      if (payload.status) updateSerialStatus(payload.status);
      if (payload.type === 'frame' && payload.frame) applyLiveFrame(payload.frame);
    } catch (error) {
      console.error(error);
    }
  };
  liveEventSource.onerror = () => {
    elements.serialStatus.textContent = '实时推送中断，可尝试重新连接';
  };
}

async function connectSerial() {
  const port = elements.serialPortSelect.value;
  if (!port) {
    elements.serialStatus.textContent = '请先选择串口端口';
    return;
  }
  const baud = Number(elements.serialBaudSelect.value) || 921600;
  elements.serialStatus.textContent = `正在连接 ${port} @ ${baud}…`;
  try {
    const response = await fetch('/api/serial/open', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ port, baud }),
    });
    const payload = await response.json();
    if (!response.ok || !payload.ok) throw new Error(payload.error || `HTTP ${response.status}`);
    liveFrames = [];
    liveApply = true;
    setSerialUi(true);
    updateSerialStatus(payload.status);
    startLiveStream();
  } catch (error) {
    setSerialUi(false);
    elements.serialStatus.textContent = `连接失败：${error.message}`;
  }
}

async function requestCalibrate() {
  if (!liveApply) {
    elements.serialStatus.textContent = '请先连接串口';
    return;
  }
  elements.serialCalibrateButton.disabled = true;
  elements.serialStatus.textContent = '正在请求重新标定…';
  try {
    const response = await fetch('/api/serial/calibrate', { method: 'POST' });
    const payload = await response.json();
    if (!response.ok || !payload.ok) throw new Error(payload.error || `HTTP ${response.status}`);
    updateSerialStatus(payload.status);
  } catch (error) {
    elements.serialCalibrateButton.disabled = false;
    elements.serialStatus.textContent = `标定请求失败：${error.message}`;
  }
}

async function disconnectSerial() {
  stopLiveStream();
  try {
    await fetch('/api/serial/close', { method: 'POST' });
  } catch (_) {
    /* ignore */
  }
  setSerialUi(false);
  liveApply = false;
  updateSerialStatus({ phase: 'idle', message: '串口未连接', sample_count: 0, output_frame_count: 0 });
  elements.sceneStatus.textContent = '等待数据';
}

elements.refreshPortsButton.addEventListener('click', refreshSerialPorts);
elements.serialConnectButton.addEventListener('click', connectSerial);
elements.serialDisconnectButton.addEventListener('click', disconnectSerial);
elements.serialCalibrateButton.addEventListener('click', requestCalibrate);
refreshSerialPorts({ autoConnect: true });

window.addEventListener('resize', resizeRenderer);
buildFingerRows();
resizeRenderer();

function animate(now) {
  const deltaS = Math.min(0.1, (now - lastFrameTime) / 1000);
  lastFrameTime = now;
  controls.update();
  renderer.render(scene, camera);

  fpsAccumulator += 1 / Math.max(deltaS, 1e-6);
  fpsFrames += 1;
  if (now - lastFpsUpdate >= 700) {
    elements.fpsValue.textContent = `${Math.round(fpsAccumulator / fpsFrames)} FPS`;
    fpsAccumulator = 0;
    fpsFrames = 0;
    lastFpsUpdate = now;
  }
  requestAnimationFrame(animate);
}
requestAnimationFrame(animate);
