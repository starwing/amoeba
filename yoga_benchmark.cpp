#include <yoga/Yoga.h>

#define ANKERL_NANOBENCH_IMPLEMENT
#include "nanobench.h"

YGNodeRef build_layout(float width, float height) {
    YGConfigRef config = YGConfigNew();
    YGConfigSetUseWebDefaults(config, false);
    
    YGNodeRef root_node = YGNodeNewWithConfig(config);
    YGNodeStyleSetWidth(root_node, width);
    YGNodeStyleSetHeight(root_node, height);
    YGNodeStyleSetPadding(root_node, YGEdgeAll, 10);
    YGNodeStyleSetGap(root_node, YGGutterAll, 10);
    YGNodeStyleSetFlexDirection(root_node, YGFlexDirectionColumn);

    // row 1
    YGNodeRef row1 = YGNodeNew();
    YGNodeStyleSetFlexDirection(row1, YGFlexDirectionRow);
    YGNodeStyleSetGap(row1, YGGutterAll, 10);
    
    YGNodeRef row1_left = YGNodeNew();
    YGNodeStyleSetFlexGrow(row1_left, 1);
    YGNodeStyleSetFlexBasis(row1_left, 0);
    YGNodeStyleSetJustifyContent(row1_left, YGJustifyCenter);
    YGNodeStyleSetAlignItems(row1_left, YGAlignFlexEnd);
    
    YGNodeRef lb1 = YGNodeNew();
    YGNodeStyleSetMinWidth(lb1, 67);
    YGNodeStyleSetMinHeight(lb1, 16);
    YGNodeInsertChild(row1_left, lb1, 0);
    
    YGNodeRef row1_right = YGNodeNew();
    YGNodeStyleSetFlexGrow(row1_right, 1);
    YGNodeStyleSetFlexBasis(row1_right, 0);
    YGNodeStyleSetJustifyContent(row1_right, YGJustifyCenter);
    YGNodeStyleSetAlignItems(row1_right, YGAlignFlexStart);
    
    YGNodeRef fl1 = YGNodeNew();
    YGNodeStyleSetWidth(fl1, 125);
    YGNodeStyleSetMinHeight(fl1, 21);
    YGNodeInsertChild(row1_right, fl1, 0);
    
    YGNodeInsertChild(row1, row1_left, 0);
    YGNodeInsertChild(row1, row1_right, 1);
    
    // row 2
    YGNodeRef row2 = YGNodeNew();
    YGNodeStyleSetFlexDirection(row2, YGFlexDirectionRow);
    YGNodeStyleSetGap(row2, YGGutterAll, 10);
    
    YGNodeRef row2_left = YGNodeNew();
    YGNodeStyleSetFlexGrow(row2_left, 1);
    YGNodeStyleSetFlexBasis(row2_left, 0);
    YGNodeStyleSetJustifyContent(row2_left, YGJustifyCenter);
    YGNodeStyleSetAlignItems(row2_left, YGAlignFlexEnd);
    
    YGNodeRef lb2 = YGNodeNew();
    YGNodeStyleSetMinWidth(lb2, 60);
    YGNodeStyleSetMinHeight(lb2, 16);
    YGNodeInsertChild(row2_left, lb2, 0);
    
    YGNodeRef row2_right = YGNodeNew();
    YGNodeStyleSetFlexGrow(row2_right, 1);
    YGNodeStyleSetFlexBasis(row2_right, 0);
    YGNodeStyleSetJustifyContent(row2_right, YGJustifyCenter);
    YGNodeStyleSetAlignItems(row2_right, YGAlignFlexStart);
    
    YGNodeRef fl2 = YGNodeNew();
    YGNodeStyleSetWidth(fl2, 125);
    YGNodeStyleSetMinHeight(fl2, 21);
    YGNodeInsertChild(row2_right, fl2, 0);
    
    YGNodeInsertChild(row2, row2_left, 0);
    YGNodeInsertChild(row2, row2_right, 1);
    
    // row 3
    YGNodeRef row3 = YGNodeNew();
    YGNodeStyleSetFlexDirection(row3, YGFlexDirectionRow);
    YGNodeStyleSetGap(row3, YGGutterAll, 10);

    YGNodeRef row3_left = YGNodeNew();
    YGNodeStyleSetFlexGrow(row3_left, 1);
    YGNodeStyleSetFlexBasis(row3_left, 0);
    YGNodeStyleSetJustifyContent(row3_left, YGJustifyCenter);
    YGNodeStyleSetAlignItems(row3_left, YGAlignFlexEnd);
    
    YGNodeRef lb3 = YGNodeNew();
    YGNodeStyleSetMinWidth(lb3, 24);
    YGNodeStyleSetMinHeight(lb3, 16);
    YGNodeInsertChild(row3_left, lb3, 0);
    
    YGNodeRef row3_right = YGNodeNew();
    YGNodeStyleSetFlexGrow(row3_right, 1);
    YGNodeStyleSetFlexBasis(row3_right, 0);
    YGNodeStyleSetJustifyContent(row3_right, YGJustifyCenter);
    YGNodeStyleSetAlignItems(row3_right, YGAlignFlexStart);
    
    YGNodeRef ct = YGNodeNew();
    YGNodeStyleSetWidth(ct, 119);
    YGNodeStyleSetHeight(ct, 24);
    YGNodeInsertChild(row3_right, ct, 0);
    
    YGNodeInsertChild(row3, row3_left, 0);
    YGNodeInsertChild(row3, row3_right, 1);
    
    // fl3 row
    YGNodeRef fl3_row = YGNodeNew();
    YGNodeStyleSetFlexDirection(fl3_row, YGFlexDirectionRow);
    YGNodeStyleSetFlexGrow(fl3_row, 1);
    YGNodeStyleSetJustifyContent(fl3_row, YGJustifyFlexEnd);
    YGNodeStyleSetAlignItems(fl3_row, YGAlignFlexEnd);
    
    YGNodeRef fl3 = YGNodeNew();
    YGNodeStyleSetWidth(fl3, 125);
    YGNodeStyleSetMinHeight(fl3, 21);
    YGNodeInsertChild(fl3_row, fl3, 0);
    
    // makeup root node
    YGNodeInsertChild(root_node, row1, 0);
    YGNodeInsertChild(root_node, row2, 1);
    YGNodeInsertChild(root_node, row3, 2);
    YGNodeInsertChild(root_node, fl3_row, 3);
    
    YGNodeCalculateLayout(root_node, YGUndefined, YGUndefined, YGDirectionLTR);
    return root_node;
}

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
