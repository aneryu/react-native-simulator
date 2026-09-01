#!/usr/bin/env node
import {readFile} from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';

const projectRoot = process.cwd();
const testerRoot = path.join(
  projectRoot,
  'third_party',
  'react-native',
  'packages',
  'rn-tester',
);

// Phase 4 Android baseline is RN Tester, not repository sample apps.
// Status is the current simulator mapping; missing/unknown is not allowed.
const ANDROID_EXAMPLE_BASELINE = {
  DrawerLayoutAndroid: {
    group: 'Components',
    official: ['AndroidDrawerLayout'],
    status: 'skia-drawer-layout',
  },
  PopupMenuAndroidExample: {
    group: 'Components',
    official: ['AndroidPopupMenu'],
    owner: 'rntester-addon',
    status: 'descriptor-only-mock',
  },
  ActivityIndicatorExample: {
    group: 'Components',
    official: ['AndroidProgressBar'],
    status: 'skia-progress-indicator',
  },
  ButtonExample: {
    group: 'Components',
    official: ['View', 'Text', 'Pressable'],
    status: 'js-composition-on-real-view-text',
  },
  FlatListExampleIndex: {
    group: 'Components',
    official: ['ScrollView', 'VirtualizedList'],
    status: 'headless-viewport-state',
  },
  ImageExample: {
    group: 'Components',
    official: ['Image', 'ImageLoader'],
    status: 'skia-local-and-http-image',
  },
  LayoutConformanceExample: {
    group: 'Components',
    official: ['LayoutConformance'],
    status: 'framework-internal',
  },
  JSResponderHandlerExample: {
    group: 'Components',
    official: ['View'],
    status: 'real-fabric-yoga',
  },
  KeyboardAvoidingViewExample: {
    group: 'Components',
    official: ['KeyboardObserver', 'View'],
    status: 'headless-keyboard-metrics',
  },
  KeyEvents: {
    group: 'Components',
    official: ['View'],
    status: 'partial-key-events',
  },
  ModalExample: {
    group: 'Components',
    official: ['ModalHostView'],
    status: 'skia-modal-host',
  },
  NewAppScreenExample: {
    group: 'Components',
    official: ['View', 'Text', 'Image'],
    owner: 'rn-new-app-screen',
    status: 'js-package-on-core-components',
  },
  PressableExample: {
    group: 'Components',
    official: ['View', 'Pressability'],
    status: 'js-composition-on-real-view',
  },
  RefreshControlExample: {
    group: 'Components',
    official: ['AndroidSwipeRefreshLayout', 'PullToRefreshView'],
    status: 'skia-refresh-control',
  },
  ScrollViewExample: {
    group: 'Components',
    official: ['ScrollView'],
    status: 'headless-viewport-state',
  },
  ScrollViewSimpleExample: {
    group: 'Components',
    official: ['ScrollView'],
    status: 'headless-viewport-state',
  },
  ScrollViewAnimatedExample: {
    group: 'Components',
    official: ['ScrollView'],
    status: 'headless-viewport-state',
  },
  SectionListExample: {
    group: 'Components',
    official: ['ScrollView', 'VirtualizedList'],
    status: 'headless-viewport-state',
  },
  StatusBarExample: {
    group: 'Components',
    official: ['StatusBarManager'],
    status: 'skia-status-bar',
  },
  SwipeableCardExample: {
    group: 'Components',
    official: ['View', 'PanResponder'],
    status: 'js-composition-on-real-view',
  },
  SwitchExample: {
    group: 'Components',
    official: ['AndroidSwitch'],
    status: 'skia-switch',
  },
  TextExample: {
    group: 'Components',
    official: ['Text', 'Paragraph'],
    status: 'skia-prepared-text',
  },
  TextInputExample: {
    group: 'Components',
    official: ['AndroidTextInput'],
    status: 'rn-fabric-headless-platform-adapter',
  },
  'TextInputs with key prop': {
    group: 'Components',
    official: ['AndroidTextInput'],
    status: 'rn-fabric-headless-platform-adapter',
  },
  TouchableExample: {
    group: 'Components',
    official: ['View', 'SoundManager'],
    status: 'js-composition-on-real-view',
  },
  ViewExample: {
    group: 'Components',
    official: ['View'],
    status: 'skia-android-view-approx',
  },
  NewArchitectureExample: {
    group: 'Components',
    official: ['RNTMyNativeView', 'RNTMyLegacyNativeView'],
    owner: 'rntester-addon',
    status: 'descriptor-only-mock',
  },
  PerformanceComparisonExample: {
    group: 'Components',
    official: ['View', 'Text'],
    status: 'js-composition-on-real-view-text',
  },
  AccessibilityExample: {
    group: 'APIs',
    official: ['AccessibilityInfo'],
    status: 'host-environment-events',
  },
  AccessibilityAndroidExample: {
    group: 'APIs',
    official: ['AccessibilityInfo'],
    status: 'host-environment-events',
  },
  AlertExample: {
    group: 'APIs',
    official: ['DialogManagerAndroid'],
    status: 'imgui-or-mock',
  },
  AnimatedIndex: {
    group: 'APIs',
    official: ['NativeAnimatedModule'],
    status: 'real-headless',
  },
  AnimationBackendIndex: {
    group: 'APIs',
    official: ['NativeAnimatedModule'],
    status: 'shared-animation-backend',
  },
  'Animation - GratuitousAnimation': {
    group: 'APIs',
    official: ['NativeAnimatedModule'],
    status: 'real-headless',
  },
  AppearanceExample: {
    group: 'APIs',
    official: ['Appearance'],
    status: 'host-environment-events',
  },
  AppStateExample: {
    group: 'APIs',
    official: ['AppState'],
    status: 'host-environment-events',
  },
  ContentURLAndroid: {
    group: 'APIs',
    official: ['IntentAndroid'],
    status: 'headless-adapter',
  },
  URLExample: {
    group: 'APIs',
    official: ['BlobModule'],
    status: 'in-memory-blob-store',
  },
  BorderExample: {
    group: 'APIs',
    official: ['View'],
    status: 'skia-per-edge-border',
  },
  CrashExample: {
    group: 'APIs',
    official: ['ExceptionsManager'],
    status: 'headless-adapter',
  },
  DevSettings: {
    group: 'APIs',
    official: ['DevSettings'],
    status: 'headless-adapter',
  },
  Dimensions: {
    group: 'APIs',
    official: ['DeviceInfo'],
    status: 'headless-adapter',
  },
  DisplayContentsExample: {
    group: 'APIs',
    official: ['View'],
    status: 'real-fabric-yoga',
  },
  FocusEventsExample: {
    group: 'APIs',
    official: ['View', 'AndroidTextInput'],
    status: 'partial-focus-events',
  },
  InvalidPropsExample: {
    group: 'APIs',
    official: ['View'],
    status: 'real-fabric-yoga',
  },
  Keyboard: {
    group: 'APIs',
    official: ['KeyboardObserver'],
    status: 'headless-keyboard-metrics',
  },
  LayoutEventsExample: {
    group: 'APIs',
    official: ['View'],
    status: 'real-fabric-yoga',
  },
  LinkingExample: {
    group: 'APIs',
    official: ['IntentAndroid'],
    status: 'imgui-or-mock',
  },
  LayoutAnimationExample: {
    group: 'APIs',
    official: ['UIManager'],
    status: 'layout-animation-driver',
  },
  LayoutExample: {
    group: 'APIs',
    official: ['View'],
    status: 'real-fabric-yoga',
  },
  NativeAnimationsExample: {
    group: 'APIs',
    official: ['NativeAnimatedModule'],
    status: 'real-headless',
  },
  OrientationChangeExample: {
    group: 'APIs',
    official: ['DeviceInfo'],
    status: 'host-environment-events',
  },
  PanResponderExample: {
    group: 'APIs',
    official: ['View', 'PanResponder'],
    status: 'js-composition-on-real-view',
  },
  PixelRatio: {
    group: 'APIs',
    official: ['DeviceInfo'],
    status: 'headless-adapter',
  },
  PermissionsExampleAndroid: {
    group: 'APIs',
    official: ['PermissionsAndroid'],
    status: 'imgui-or-mock',
  },
  PlatformColorExample: {
    group: 'APIs',
    official: ['View'],
    status: 'skia-android-theme-tokens',
  },
  PointerEventsExample: {
    group: 'APIs',
    official: ['View'],
    status: 'real-fabric-yoga',
  },
  RTLExample: {
    group: 'APIs',
    official: ['I18nManager', 'View'],
    status: 'headless-i18n-layout',
  },
  ShareExample: {
    group: 'APIs',
    official: ['ShareModule'],
    status: 'imgui-or-mock',
  },
  TimerExample: {
    group: 'APIs',
    official: ['Timing'],
    status: 'real-timer-manager',
  },
  ToastAndroidExample: {
    group: 'APIs',
    official: ['ToastAndroid'],
    status: 'skia-toast',
  },
  TransformExample: {
    group: 'APIs',
    official: ['View'],
    status: 'real-fabric-yoga',
  },
  FilterExample: {
    group: 'APIs',
    official: ['View'],
    status: 'skia-css-filters',
  },
  LinearGradientExample: {
    group: 'APIs',
    official: ['View'],
    status: 'skia-linear-gradient',
  },
  RadialGradientExample: {
    group: 'APIs',
    official: ['View'],
    status: 'skia-radial-gradient',
  },
  BackgroundImageExample: {
    group: 'APIs',
    official: ['View', 'Image'],
    status: 'skia-background-image',
  },
  MixBlendModeExample: {
    group: 'APIs',
    official: ['View'],
    status: 'skia-mix-blend-mode',
  },
  VibrationExample: {
    group: 'APIs',
    official: ['Vibration'],
    status: 'imgui-or-mock',
  },
  WebSocketExample: {
    group: 'APIs',
    official: ['WebSocketModule'],
    status: 'urlsession-websocket',
  },
  XHRExample: {
    group: 'APIs',
    official: ['Networking'],
    status: 'urlsession-http',
  },
  TurboModuleExample: {
    group: 'APIs',
    official: ['SampleTurboModule'],
    owner: 'rn-test-internal',
    status: 'not-in-production-profile',
  },
  LegacyModuleExample: {
    group: 'APIs',
    official: ['SampleTurboModule'],
    owner: 'rn-test-internal',
    status: 'not-in-production-profile',
  },
  TurboCxxModuleExample: {
    group: 'APIs',
    official: ['NativeCxxModuleExampleCxx'],
    owner: 'rntester-addon',
    status: 'tester-stub',
  },
  IntersectionObserver: {
    group: 'APIs',
    official: ['NativeIntersectionObserverCxx'],
    status: 'rn-intersection-observer',
  },
  MutationObserver: {
    group: 'APIs',
    official: ['NativeMutationObserverCxx'],
    status: 'rn-mutation-observer',
  },
  PerformanceApiExample: {
    group: 'APIs',
    official: ['NativePerformanceCxx'],
    status: 'host-performance-timeline',
  },
  PlaygroundExample: {
    group: 'Playgrounds',
    official: ['View'],
    status: 'js-composition-on-real-view',
  },
};

const listPath = path.join(testerRoot, 'js/utils/RNTesterList.android.js');
const source = await readFile(listPath, 'utf8');
const listedKeys = [...source.matchAll(/key:\s*'([^']+)'/g)].map(
  match => match[1],
);
const uniqueListed = [...new Set(listedKeys)];
const mappedKeys = Object.keys(ANDROID_EXAMPLE_BASELINE);
const missingFromBaseline = uniqueListed.filter(
  key => !Object.hasOwn(ANDROID_EXAMPLE_BASELINE, key),
);
const extraInBaseline = mappedKeys.filter(key => !uniqueListed.includes(key));
if (missingFromBaseline.length > 0 || extraInBaseline.length > 0) {
  throw new Error(
    'RN Tester Android baseline is out of date:\n' +
      (missingFromBaseline.length
        ? `  missing: ${missingFromBaseline.join(', ')}\n`
        : '') +
      (extraInBaseline.length
        ? `  extra: ${extraInBaseline.join(', ')}\n`
        : ''),
  );
}

const byStatus = {};
const examples = uniqueListed.map(key => {
  const entry = ANDROID_EXAMPLE_BASELINE[key];
  byStatus[entry.status] = (byStatus[entry.status] ?? 0) + 1;
  return {key, ...entry};
});

const result = {
  source: 'packages/rn-tester/js/utils/RNTesterList.android.js',
  appKey: 'RNTesterApp',
  exampleCount: examples.length,
  byStatus,
  examples,
};

process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
