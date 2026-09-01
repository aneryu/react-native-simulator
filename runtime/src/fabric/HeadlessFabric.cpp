#include "HeadlessFabric.h"

#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/components/root/RootComponentDescriptor.h>
#include <react/renderer/components/view/LayoutConformanceComponentDescriptor.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/element/ComponentBuilder.h>
#include <react/renderer/element/Element.h>
#include <react/renderer/mounting/ShadowTree.h>
#include <react/renderer/mounting/ShadowTreeDelegate.h>

#include <cmath>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

namespace react = facebook::react;
namespace yoga = facebook::yoga;

namespace {

constexpr react::SurfaceId kSurfaceId = 11;
constexpr react::Tag kFixedTag = 12;
constexpr react::Tag kFlexTag = 13;

struct MountedNode {
  react::Tag parentTag{-1};
  int index{-1};
  react::ShadowView shadowView{};
};

class HeadlessMountingDelegate final : public react::ShadowTreeDelegate {
 public:
  react::RootShadowNode::Unshared shadowTreeWillCommit(
      const react::ShadowTree&,
      const react::RootShadowNode::Shared&,
      const react::RootShadowNode::Unshared& newRootShadowNode,
      const react::ShadowTree::CommitOptions&) const override {
    return newRootShadowNode;
  }

  void shadowTreeDidFinishTransaction(
      std::shared_ptr<const react::MountingCoordinator> mountingCoordinator,
      bool) const override {
    auto transaction = mountingCoordinator->pullTransaction();
    if (!transaction) {
      return;
    }

    ++transactions;
    for (const auto& mutation : transaction->getMutations()) {
      switch (mutation.type) {
        case react::ShadowViewMutation::Create:
          ++creates;
          nodes[mutation.newChildShadowView.tag].shadowView =
              mutation.newChildShadowView;
          break;
        case react::ShadowViewMutation::Insert: {
          ++inserts;
          auto& node = nodes[mutation.newChildShadowView.tag];
          node.parentTag = mutation.parentTag;
          node.index = mutation.index;
          node.shadowView = mutation.newChildShadowView;
          break;
        }
        case react::ShadowViewMutation::Update: {
          ++updates;
          auto& node = nodes[mutation.newChildShadowView.tag];
          node.parentTag = mutation.parentTag;
          node.shadowView = mutation.newChildShadowView;
          break;
        }
        case react::ShadowViewMutation::Remove:
          ++removes;
          nodes[mutation.oldChildShadowView.tag].parentTag = -1;
          nodes[mutation.oldChildShadowView.tag].index = -1;
          break;
        case react::ShadowViewMutation::Delete:
          ++deletes;
          nodes.erase(mutation.oldChildShadowView.tag);
          break;
      }
    }
  }

  void shadowTreeDidFinishReactCommit(const react::ShadowTree&) const override {}
  void shadowTreeDidPromoteReactRevision(const react::ShadowTree&) const override {}

  mutable std::size_t transactions{0};
  mutable std::size_t creates{0};
  mutable std::size_t inserts{0};
  mutable std::size_t updates{0};
  mutable std::size_t removes{0};
  mutable std::size_t deletes{0};
  mutable std::unordered_map<react::Tag, MountedNode> nodes;
};

struct FabricTree {
  react::Props::Shared rootProps;
  std::shared_ptr<react::ViewShadowNode> fixed;
  std::shared_ptr<react::ViewShadowNode> flex;
};

FabricTree buildTree(const react::ComponentBuilder& builder, float fixedWidth) {
  FabricTree tree;
  auto rootProps = std::make_shared<react::RootProps>();
  rootProps->layoutConstraints = {
      .minimumSize = {.width = 0, .height = 0},
      .maximumSize = {.width = 300, .height = 80}};
  rootProps->yogaStyle.setFlexDirection(yoga::FlexDirection::Row);
  rootProps->yogaStyle.setDimension(
      yoga::Dimension::Width, yoga::StyleSizeLength::points(300));
  rootProps->yogaStyle.setDimension(
      yoga::Dimension::Height, yoga::StyleSizeLength::points(80));
  tree.rootProps = std::move(rootProps);

  auto fixed = react::Element<react::ViewShadowNode>()
                   .reference(tree.fixed)
                   .tag(kFixedTag)
                   .surfaceId(kSurfaceId)
                   .props([fixedWidth] {
                     auto props = std::make_shared<react::ViewProps>();
                     props->collapsable = false;
                     props->yogaStyle.setDimension(
                         yoga::Dimension::Width,
                         yoga::StyleSizeLength::points(fixedWidth));
                     props->yogaStyle.setDimension(
                         yoga::Dimension::Height,
                         yoga::StyleSizeLength::points(40));
                     return props;
                   });
  auto flex = react::Element<react::ViewShadowNode>()
                  .reference(tree.flex)
                  .tag(kFlexTag)
                  .surfaceId(kSurfaceId)
                  .props([] {
                    auto props = std::make_shared<react::ViewProps>();
                    props->collapsable = false;
                    props->yogaStyle.setFlexGrow(yoga::FloatOptional{1});
                    props->yogaStyle.setDimension(
                        yoga::Dimension::Height,
                        yoga::StyleSizeLength::points(40));
                    return props;
                  });
  builder.build(fixed);
  builder.build(flex);
  return tree;
}

FabricTree updateTree(const FabricTree& previous, float fixedWidth) {
  FabricTree tree;
  tree.rootProps = previous.rootProps;
  auto fixedProps = std::make_shared<react::ViewProps>();
  fixedProps->collapsable = false;
  fixedProps->yogaStyle.setDimension(
      yoga::Dimension::Width, yoga::StyleSizeLength::points(fixedWidth));
  fixedProps->yogaStyle.setDimension(
      yoga::Dimension::Height, yoga::StyleSizeLength::points(40));
  tree.fixed = std::static_pointer_cast<react::ViewShadowNode>(
      previous.fixed->react::ShadowNode::clone({.props = fixedProps}));
  tree.flex = std::static_pointer_cast<react::ViewShadowNode>(
      previous.flex->react::ShadowNode::clone({}));
  return tree;
}

bool near(float actual, float expected) {
  return std::fabs(actual - expected) < 0.01F;
}

// RN Tester LayoutConformanceBox: a 60-wide row with a nested row whose only
// child is flexGrow:1. YGErrataStretchFlexBasis (Unset/Compat) stretches that
// child to 60; Strict drops StretchFlexBasis so the child stays 0-wide.
float layoutConformanceGrowWidth(
    const react::ComponentBuilder& builder,
    const std::shared_ptr<const react::ContextContainer>& contextContainer,
    std::optional<react::LayoutConformance> mode,
    react::SurfaceId surfaceId,
    react::Tag baseTag) {
  HeadlessMountingDelegate delegate;
  react::ShadowTree shadowTree{
      surfaceId,
      {.minimumSize = {.width = 0, .height = 0},
       .maximumSize = {.width = 300, .height = 80}},
      {},
      delegate,
      *contextContainer};

  std::shared_ptr<react::ViewShadowNode> grow;
  auto box = react::Element<react::ViewShadowNode>()
                 .tag(baseTag)
                 .surfaceId(surfaceId)
                 .props([] {
                   auto props = std::make_shared<react::ViewProps>();
                   props->collapsable = false;
                   props->yogaStyle.setFlexDirection(yoga::FlexDirection::Row);
                   props->yogaStyle.setAlignItems(yoga::Align::Center);
                   props->yogaStyle.setDimension(
                       yoga::Dimension::Width,
                       yoga::StyleSizeLength::points(60));
                   props->yogaStyle.setDimension(
                       yoga::Dimension::Height,
                       yoga::StyleSizeLength::points(60));
                   return props;
                 })
                 .children({react::Element<react::ViewShadowNode>()
                                .tag(baseTag + 1)
                                .surfaceId(surfaceId)
                                .props([] {
                                  auto props =
                                      std::make_shared<react::ViewProps>();
                                  props->collapsable = false;
                                  props->yogaStyle.setFlexDirection(
                                      yoga::FlexDirection::Row);
                                  return props;
                                })
                                .children(
                                    {react::Element<react::ViewShadowNode>()
                                         .reference(grow)
                                         .tag(baseTag + 2)
                                         .surfaceId(surfaceId)
                                         .props([] {
                                           auto props = std::make_shared<
                                               react::ViewProps>();
                                           props->collapsable = false;
                                           props->yogaStyle.setFlexGrow(
                                               yoga::FloatOptional{1});
                                           props->yogaStyle.setDimension(
                                               yoga::Dimension::Height,
                                               yoga::StyleSizeLength::points(
                                                   30));
                                           return props;
                                         })})});

  std::shared_ptr<const react::ShadowNode> child;
  if (mode) {
    child = builder.build(
        react::Element<react::LayoutConformanceShadowNode>()
            .tag(baseTag + 3)
            .surfaceId(surfaceId)
            .props([conformance = *mode] {
              auto props = std::make_shared<react::LayoutConformanceProps>();
              props->mode = conformance;
              props->yogaStyle.setDisplay(yoga::Display::Contents);
              return props;
            })
            .children({box}));
  } else {
    child = builder.build(box);
  }

  auto rootProps = std::make_shared<react::RootProps>();
  rootProps->layoutConstraints = {
      .minimumSize = {.width = 0, .height = 0},
      .maximumSize = {.width = 300, .height = 80}};
  rootProps->yogaStyle.setDimension(
      yoga::Dimension::Width, yoga::StyleSizeLength::points(300));
  rootProps->yogaStyle.setDimension(
      yoga::Dimension::Height, yoga::StyleSizeLength::points(80));
  const auto status = shadowTree.commit(
      [&](const react::RootShadowNode& oldRoot)
          -> react::RootShadowNode::Unshared {
        auto children = std::make_shared<
            const std::vector<std::shared_ptr<const react::ShadowNode>>>(
            std::initializer_list<std::shared_ptr<const react::ShadowNode>>{
                child});
        return std::static_pointer_cast<react::RootShadowNode>(
            oldRoot.react::ShadowNode::clone({
                .props = rootProps,
                .children = children,
            }));
      },
      {});
  if (status != react::ShadowTree::CommitStatus::Succeeded || grow == nullptr) {
    return -1;
  }
  return grow->getLayoutMetrics().frame.size.width;
}

} // namespace

HeadlessFabricResult runHeadlessFabricPipeline() {
  HeadlessFabricResult result;
  auto contextContainer = std::make_shared<const react::ContextContainer>();
  react::ComponentDescriptorProviderRegistry providers;
  auto registry = providers.createComponentDescriptorRegistry({
      .eventDispatcher = {},
      .contextContainer = contextContainer,
      .flavor = nullptr});
  providers.add(
      react::concreteComponentDescriptorProvider<
          react::RootComponentDescriptor>());
  providers.add(
      react::concreteComponentDescriptorProvider<
          react::ViewComponentDescriptor>());
  providers.add(
      react::concreteComponentDescriptorProvider<
          react::LayoutConformanceComponentDescriptor>());
  react::ComponentBuilder builder{registry};
  HeadlessMountingDelegate delegate;
  react::ShadowTree shadowTree{
      kSurfaceId,
      {.minimumSize = {.width = 0, .height = 0},
       .maximumSize = {.width = 300, .height = 80}},
      {},
      delegate,
      *contextContainer};

  auto first = buildTree(builder, 100);
  react::ShadowTreeCommitTransaction initialCommit =
      [&first](const react::RootShadowNode& oldRoot)
      -> react::RootShadowNode::Unshared {
    auto children = std::make_shared<
        const std::vector<std::shared_ptr<const react::ShadowNode>>>(
        std::initializer_list<std::shared_ptr<const react::ShadowNode>>{
            first.fixed, first.flex});
    return std::static_pointer_cast<react::RootShadowNode>(
        oldRoot.react::ShadowNode::clone({
            .props = first.rootProps,
            .children = children,
        }));
  };
  auto status = shadowTree.commit(initialCommit, {});
  if (status != react::ShadowTree::CommitStatus::Succeeded) {
    result.error = "initial Fabric commit failed";
    return result;
  }
  result.firstWidth = first.fixed->getLayoutMetrics().frame.size.width;
  result.firstFlexWidth = first.flex->getLayoutMetrics().frame.size.width;

  auto updated = updateTree(first, 120);
  react::ShadowTreeCommitTransaction updateCommit =
      [&updated](const react::RootShadowNode& oldRoot)
      -> react::RootShadowNode::Unshared {
    auto children = std::make_shared<
        const std::vector<std::shared_ptr<const react::ShadowNode>>>(
        std::initializer_list<std::shared_ptr<const react::ShadowNode>>{
            updated.fixed, updated.flex});
    return std::static_pointer_cast<react::RootShadowNode>(
        oldRoot.react::ShadowNode::clone({
            .props = updated.rootProps,
            .children = children,
        }));
  };
  status = shadowTree.commit(updateCommit, {});
  if (status != react::ShadowTree::CommitStatus::Succeeded) {
    result.error = "updated Fabric commit failed";
    return result;
  }
  result.updatedWidth = updated.fixed->getLayoutMetrics().frame.size.width;
  result.updatedFlexWidth = updated.flex->getLayoutMetrics().frame.size.width;
  result.transactions = delegate.transactions;
  result.creates = delegate.creates;
  result.inserts = delegate.inserts;
  result.updates = delegate.updates;

  const auto fixed = delegate.nodes.find(kFixedTag);
  const auto flex = delegate.nodes.find(kFlexTag);
  const bool mountedFramesAreCurrent =
      fixed != delegate.nodes.end() && flex != delegate.nodes.end() &&
      near(fixed->second.shadowView.layoutMetrics.frame.size.width, 120) &&
      near(flex->second.shadowView.layoutMetrics.frame.size.width, 180);
  result.passed =
      result.transactions == 2 && result.creates >= 2 &&
      result.inserts >= 2 && result.updates >= 2 &&
      near(result.firstWidth, 100) && near(result.firstFlexWidth, 200) &&
      near(result.updatedWidth, 120) && near(result.updatedFlexWidth, 180) &&
      mountedFramesAreCurrent;
  if (!result.passed) {
    result.error = "Fabric/Yoga mutation or frame invariant failed";
    return result;
  }

  const auto unsetWidth = layoutConformanceGrowWidth(
      builder, contextContainer, std::nullopt, 21, 22);
  const auto compatWidth = layoutConformanceGrowWidth(
      builder,
      contextContainer,
      react::LayoutConformance::Compatibility,
      31,
      32);
  const auto strictWidth = layoutConformanceGrowWidth(
      builder, contextContainer, react::LayoutConformance::Strict, 41, 42);
  if (!near(unsetWidth, 60) || !near(compatWidth, 60) ||
      !near(strictWidth, 0)) {
    result.passed = false;
    result.error =
        "LayoutConformance Yoga errata did not change Strict vs Compat/Unset";
  }
  return result;
}
