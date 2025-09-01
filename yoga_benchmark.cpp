#include <yoga/Yoga.h>

#define ANKERL_NANOBENCH_IMPLEMENT
#include "nanobench.h"

YGNodeRef build_layout(float width, float height) {
    // 创建配置
    YGConfigRef config = YGConfigNew();
    YGConfigSetUseWebDefaults(config, false);
    
    // 创建根节点
    YGNodeRef root_node = YGNodeNewWithConfig(config);
    YGNodeStyleSetWidth(root_node, width);
    YGNodeStyleSetHeight(root_node, height);
    YGNodeStyleSetPadding(root_node, YGEdgeAll, 10);
    YGNodeStyleSetFlexDirection(root_node, YGFlexDirectionColumn);
    
    // 第一行
    YGNodeRef row1 = YGNodeNew();
    YGNodeStyleSetFlexDirection(row1, YGFlexDirectionRow);
    YGNodeStyleSetMargin(row1, YGEdgeBottom, 10);
    YGNodeStyleSetAlignItems(row1, YGAlignCenter);
    
    // 第一行左半部分
    YGNodeRef row1_left = YGNodeNew();
    YGNodeStyleSetWidthPercent(row1_left, 50);
    YGNodeStyleSetAlignItems(row1_left, YGAlignFlexEnd);
    YGNodeStyleSetPadding(row1_left, YGEdgeRight, 10);
    
    // lb1
    YGNodeRef lb1 = YGNodeNew();
    YGNodeStyleSetMinWidth(lb1, 67);
    YGNodeStyleSetMinHeight(lb1, 16);
    YGNodeInsertChild(row1_left, lb1, 0);
    
    // 第一行右半部分
    YGNodeRef row1_right = YGNodeNew();
    YGNodeStyleSetWidthPercent(row1_right, 50);
    
    // fl1
    YGNodeRef fl1 = YGNodeNew();
    YGNodeStyleSetWidth(fl1, 125);
    YGNodeStyleSetMinHeight(fl1, 21);
    YGNodeInsertChild(row1_right, fl1, 0);
    
    // 组装第一行
    YGNodeInsertChild(row1, row1_left, 0);
    YGNodeInsertChild(row1, row1_right, 1);
    
    // 第二行
    YGNodeRef row2 = YGNodeNew();
    YGNodeStyleSetFlexDirection(row2, YGFlexDirectionRow);
    YGNodeStyleSetMargin(row2, YGEdgeBottom, 10);
    YGNodeStyleSetAlignItems(row2, YGAlignCenter);
    
    // 第二行左半部分
    YGNodeRef row2_left = YGNodeNew();
    YGNodeStyleSetWidthPercent(row2_left, 50);
    YGNodeStyleSetAlignItems(row2_left, YGAlignFlexEnd);
    YGNodeStyleSetPadding(row2_left, YGEdgeRight, 10);
    
    // lb2
    YGNodeRef lb2 = YGNodeNew();
    YGNodeStyleSetMinWidth(lb2, 60);
    YGNodeStyleSetMinHeight(lb2, 16);
    YGNodeInsertChild(row2_left, lb2, 0);
    
    // 第二行右半部分
    YGNodeRef row2_right = YGNodeNew();
    YGNodeStyleSetWidthPercent(row2_right, 50);
    
    // fl2
    YGNodeRef fl2 = YGNodeNew();
    YGNodeStyleSetWidth(fl2, 125);
    YGNodeStyleSetMinHeight(fl2, 21);
    YGNodeInsertChild(row2_right, fl2, 0);
    
    // 组装第二行
    YGNodeInsertChild(row2, row2_left, 0);
    YGNodeInsertChild(row2, row2_right, 1);
    
    // 第三行
    YGNodeRef row3 = YGNodeNew();
    YGNodeStyleSetFlexDirection(row3, YGFlexDirectionRow);
    YGNodeStyleSetMargin(row3, YGEdgeBottom, 10);
    YGNodeStyleSetAlignItems(row3, YGAlignCenter);
    
    // 第三行左半部分
    YGNodeRef row3_left = YGNodeNew();
    YGNodeStyleSetWidthPercent(row3_left, 50);
    YGNodeStyleSetAlignItems(row3_left, YGAlignFlexEnd);
    YGNodeStyleSetPadding(row3_left, YGEdgeRight, 10);
    
    // lb3
    YGNodeRef lb3 = YGNodeNew();
    YGNodeStyleSetMinWidth(lb3, 24);
    YGNodeStyleSetMinHeight(lb3, 16);
    YGNodeInsertChild(row3_left, lb3, 0);
    
    // 第三行右半部分
    YGNodeRef row3_right = YGNodeNew();
    YGNodeStyleSetWidthPercent(row3_right, 50);
    
    // ct
    YGNodeRef ct = YGNodeNew();
    YGNodeStyleSetWidth(ct, 119);
    YGNodeStyleSetMinHeight(ct, 24);
    YGNodeStyleSetMaxHeight(ct, 24);
    YGNodeInsertChild(row3_right, ct, 0);
    
    // 组装第三行
    YGNodeInsertChild(row3, row3_left, 0);
    YGNodeInsertChild(row3, row3_right, 1);
    
    // fl3 行
    YGNodeRef fl3_row = YGNodeNew();
    YGNodeStyleSetFlexDirection(fl3_row, YGFlexDirectionRow);
    YGNodeStyleSetFlexGrow(fl3_row, 1);
    
    // fl3 左半部分
    YGNodeRef fl3_left = YGNodeNew();
    YGNodeStyleSetWidthPercent(fl3_left, 50);
    YGNodeStyleSetPadding(fl3_left, YGEdgeRight, 10);
    
    // fl3 右半部分
    YGNodeRef fl3_right = YGNodeNew();
    YGNodeStyleSetWidthPercent(fl3_right, 50);
    YGNodeStyleSetFlexDirection(fl3_right, YGFlexDirectionRowReverse);
    
    // fl3
    YGNodeRef fl3 = YGNodeNew();
    YGNodeStyleSetWidth(fl3, 125);
    YGNodeStyleSetMinHeight(fl3, 21);
    YGNodeStyleSetAlignSelf(fl3, YGAlignFlexEnd);
    YGNodeInsertChild(fl3_right, fl3, 0);
    
    // 组装 fl3 行
    YGNodeInsertChild(fl3_row, fl3_left, 0);
    YGNodeInsertChild(fl3_row, fl3_right, 1);
    
    // 组装根节点
    YGNodeInsertChild(root_node, row1, 0);
    YGNodeInsertChild(root_node, row2, 1);
    YGNodeInsertChild(root_node, row3, 2);
    YGNodeInsertChild(root_node, fl3_row, 3);
    
    // 计算布局
    YGNodeCalculateLayout(root_node, YGUndefined, YGUndefined, YGDirectionLTR);
    return root_node;
}

// 设置建议尺寸并重新计算布局
void suggest(YGNodeRef root_node, float width, float height) {
    YGNodeStyleSetWidth(root_node, 300); /* mark dirty */
    YGNodeStyleSetWidth(root_node, width);
    YGNodeStyleSetHeight(root_node, 200); /* mark dirty */
    YGNodeStyleSetHeight(root_node, height);
    YGNodeCalculateLayout(root_node, width, height, YGDirectionLTR);
}

int main() {
    ankerl::nanobench::Bench().minEpochIterations(1000).run("building solver", [&] {
        YGNodeRef root_node = build_layout(300, 200);
        ankerl::nanobench::doNotOptimizeAway(root_node);
        YGNodeFreeRecursive(root_node);
    });

    struct Size {
        int width;
        int height;
    };

    Size sizes[] = {
        { 400, 600 },
        { 600, 400 },
        { 800, 1200 },
        { 1200, 800 },
        { 400, 800 },
        { 800, 400 }
    };

    YGNodeRef root_node = build_layout(300, 200);

    for (const Size& size : sizes) {
        double width = size.width;
        double height = size.height;

        ankerl::nanobench::Bench().minEpochIterations(100000).run("suggest value " + std::to_string(size.width) + "x" + std::to_string(size.height), [&] {
            suggest(root_node, width, height);
        });
    }

    YGNodeFreeRecursive(root_node);
    return 0;
}

// cc: flags+='-ggdb -O3 -DNDEBUG -I ~/Work/Sources/yoga -std=c++20'
// cc: input+='yogaone.cpp'
