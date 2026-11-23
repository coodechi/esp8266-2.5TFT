"""SD2 小电视外壳 — L 形，2.4 寸屏 + Micro-B ESP8266.

侧面是 L：屏幕在立板上，底座向后伸出。
开发板平放在底座里，Micro-B 母口朝后，开孔在尾盖上，圆角矩形。

屏幕:
    模块 46.90 x 71.00 x 13.50 mm，可视区 42.72 x 58.80，有效区 37.42 x 49.66
    竖放，排针朝上。开窗落在黑边里，不压像素。

开发板:
    NodeMCU 一类，58 x 31.2 mm。窄板（约 26 mm）加「窄板垫片」。
    尾插是 Micro-B 母口。元件朝上。加高排母可能超高，需要拆掉或改短针。

打印（拓竹，PLA / PETG，0.20 mm，3 层壁，15% 填充，不用支撑）:
    主体：屏幕正面贴热床。
    屏后盖、尾盖：外表面贴热床。尾盖上的孔是圆角矩形，直接在第一层成形。
    螺丝：屏后盖 2 颗、尾盖 2 颗，M2x8 盘头自攻。

组装:
    1. 屏幕从立板后面放进前框，排针朝上。底部可以先伸进底座空腔再扶正。
    2. 盖上屏后盖。
    3. 开发板平放进底座，Micro-B 朝后，对准尾盖圆孔。
    4. 盖上尾盖。充电线从背面插。
"""

from __future__ import annotations

import os

from build123d import (
    Axis,
    Box,
    BuildPart,
    BuildSketch,
    Color,
    Cylinder,
    Locations,
    Mesher,
    Mode,
    Plane,
    Pos,
    RectangleRounded,
    Rot,
    Unit,
    extrude,
)

# ---------------------------------------------------------------------------
# 参数（毫米）
# ---------------------------------------------------------------------------

SCREEN_W = 46.90
SCREEN_H = 71.00
SCREEN_T = 13.50

POCKET_CLEAR = 0.40
POCKET_W = SCREEN_W + 2 * POCKET_CLEAR
POCKET_H = SCREEN_H + 2 * POCKET_CLEAR
POCKET_D = SCREEN_T + 1.20
POCKET_CZ = -0.80

WIN_W = 40.40
WIN_H = 55.00
WIN_R = 1.20
WIN_CZ = 0.0

OUTER_W = 60.0
OUTER_H = 86.0
R_OUT = 6.0
WALL = 2.40
FRONT = 2.60
LID_T = 2.40

# 立板：屏幕 + 背后走线，后面敞开，由屏后盖关上
WIRE = 6.0
UPRIGHT_D = FRONT + POCKET_D + WIRE  # 23.3

# 底座：立板下方向后伸出，装平放的开发板
PCB_L = 58.0
PCB_W = 31.20
PCB_T = 1.60
GUIDE_CLEAR = 0.45
FOOT_INNER_H = 17.0
FOOT_TOP_WALL = 2.20
FOOT_TOP = -OUTER_H / 2 + WALL + FOOT_INNER_H + FOOT_TOP_WALL
# 板子从立板后沿再让出 2 mm，USB 端顶到尾盖内壁
BOARD_Y0 = UPRIGHT_D + 2.0
FOOT_END = BOARD_Y0 + PCB_L  # 尾盖内壁
ASSEMBLED_D = FOOT_END + LID_T

INNER_W = OUTER_W - 2 * WALL
INNER_H = OUTER_H - 2 * WALL
INNER_R = R_OUT - WALL

# 板子搁在底板以上 1.2 mm，底面焊点不蹭壁
PCB_BOTTOM = -OUTER_H / 2 + WALL + 1.20
# Micro-B 母座贴在元件面，孔中心大约在 PCB 顶面以上 1.3 mm
USB_CZ = PCB_BOTTOM + PCB_T + 1.30
USB_W = 9.20  # 沿板宽 X。插头金属宽 6.85，母座约 7.5
USB_H = 4.00  # 沿高度 Z。插头厚 1.80，母座约 2.6
USB_R = 1.00  # 圆角，不是尖角

LIP_CLEAR = 0.30
LIP_T = 1.40
LIP_D = 2.80

BOSS_R = 3.00
BOSS_HOLE_D = 1.80
SCREW_CLEAR_D = 2.50
COUNTERBORE_D = 4.80
COUNTERBORE_H = 1.55


def xz_at(y: float) -> Plane:
    """正面草图。局部 X = 世界 X，局部 Y = 世界 Z，位于世界 Y=y。"""
    return Plane.XZ.offset(-y)


def grow(depth: float) -> None:
    """沿 +Y 拉伸。Plane.XZ 的法向是 -Y，所以用量取负。"""
    extrude(amount=-depth)


def _upper_bosses():
    z = OUTER_H / 2 - WALL - 2.30
    x = 16.0
    return [(-x, z), (x, z)]


def _foot_bosses():
    x = INNER_W / 2 - BOSS_R + 0.45
    z = (PCB_BOTTOM + (-OUTER_H / 2 + WALL + FOOT_INNER_H)) / 2
    return [(-x, z), (x, z)]


def make_body():
    foot_cavity_top = -OUTER_H / 2 + WALL + FOOT_INNER_H
    with BuildPart() as bp:
        # 立板，全高
        with BuildSketch(xz_at(0)):
            RectangleRounded(OUTER_W, OUTER_H, R_OUT)
        grow(UPRIGHT_D)

        # 底座：同一轮廓只留底部，向后伸到尾盖
        with BuildPart():
            with BuildSketch(xz_at(0)):
                RectangleRounded(OUTER_W, OUTER_H, R_OUT)
            grow(FOOT_END)
            with BuildPart(mode=Mode.SUBTRACT):
                with Locations((0, FOOT_END / 2, (FOOT_TOP + OUTER_H / 2) / 2)):
                    Box(OUTER_W + 8, FOOT_END + 4, OUTER_H / 2 - FOOT_TOP + 2)

        # 立板内腔，后面敞开
        with BuildPart(mode=Mode.SUBTRACT):
            with BuildSketch(xz_at(FRONT)):
                RectangleRounded(INNER_W, INNER_H, INNER_R)
            grow(UPRIGHT_D - FRONT + 1.0)

        # 底座内腔，和立板内腔打通，尾部敞开
        with BuildPart(mode=Mode.SUBTRACT):
            with Locations(
                (
                    0,
                    (UPRIGHT_D - 1.2 + FOOT_END) / 2 + 0.6,
                    (-OUTER_H / 2 + WALL + foot_cavity_top) / 2,
                )
            ):
                Box(
                    INNER_W,
                    FOOT_END - (UPRIGHT_D - 1.2) + 2.0,
                    foot_cavity_top - (-OUTER_H / 2 + WALL),
                )

        # 屏幕套筒
        with BuildPart():
            with BuildSketch(xz_at(FRONT - 0.05)):
                RectangleRounded(INNER_W + 0.30, INNER_H + 0.30, INNER_R)
            grow(POCKET_D + 0.05)
            with BuildPart(mode=Mode.SUBTRACT):
                with BuildSketch(xz_at(FRONT - 0.10)):
                    with Locations((0, POCKET_CZ)):
                        RectangleRounded(POCKET_W, POCKET_H, 0.50)
                grow(POCKET_D + 0.40)

        # 屏框浅槽
        with BuildPart(mode=Mode.SUBTRACT):
            with BuildSketch(xz_at(-0.05)):
                with Locations((0, WIN_CZ)):
                    RectangleRounded(WIN_W + 5.0, WIN_H + 5.0, 2.4)
            grow(0.60)

        # 开窗
        with BuildPart(mode=Mode.SUBTRACT):
            with BuildSketch(xz_at(-0.20)):
                with Locations((0, WIN_CZ)):
                    RectangleRounded(WIN_W, WIN_H, WIN_R)
            grow(FRONT + 0.80)

        # 左右金属卡扣让位
        for side in (-1, 1):
            with BuildPart(mode=Mode.SUBTRACT):
                with Locations((side * (POCKET_W / 2), FRONT + 3.0, POCKET_CZ)):
                    Box(3.2, 8.0, 16.0)

        # 开发板侧导轨，底部吃进底板
        guide_inner = PCB_W / 2 + GUIDE_CLEAR
        guide_top = PCB_BOTTOM + 8.0
        guide_bottom = -OUTER_H / 2 + WALL - 0.6
        for side in (-1, 1):
            with BuildPart():
                with Locations(
                    (
                        side * (guide_inner + 1.15),
                        (BOARD_Y0 + FOOT_END) / 2 - 1.0,
                        (guide_top + guide_bottom) / 2,
                    )
                ):
                    Box(2.3, FOOT_END - BOARD_Y0 - 2.0, guide_top - guide_bottom)

        # 承台：板子底边搭在上面
        rail_top = PCB_BOTTOM
        rail_bottom = -OUTER_H / 2 + WALL - 0.4
        for side in (-1, 1):
            with BuildPart():
                with Locations(
                    (
                        side * 11.0,
                        (BOARD_Y0 + FOOT_END) / 2 - 1.0,
                        (rail_top + rail_bottom) / 2,
                    )
                ):
                    Box(8.0, FOOT_END - BOARD_Y0 - 2.0, rail_top - rail_bottom)

        # 立板顶部两颗螺丝柱
        for bx, bz in _upper_bosses():
            with Locations((bx, UPRIGHT_D - 6.0, bz)):
                Cylinder(BOSS_R, 12.0, rotation=(90, 0, 0))
            with Locations((bx, UPRIGHT_D - 6.0, bz)):
                Cylinder(
                    BOSS_HOLE_D / 2,
                    13.0,
                    rotation=(90, 0, 0),
                    mode=Mode.SUBTRACT,
                )

        # 尾盖两颗螺丝柱，在开发板两侧
        for bx, bz in _foot_bosses():
            with Locations((bx, FOOT_END - 6.0, bz)):
                Cylinder(BOSS_R, 12.0, rotation=(90, 0, 0))
            with Locations((bx, FOOT_END - 6.0, bz)):
                Cylinder(
                    BOSS_HOLE_D / 2,
                    13.0,
                    rotation=(90, 0, 0),
                    mode=Mode.SUBTRACT,
                )

    part = bp.part
    # 立板和底座交接处倒一个小圆角，L 形不那么生硬
    try:
        seam = [
            e
            for e in part.edges().filter_by(Axis.X)
            if abs(e.center().Y - UPRIGHT_D) < 0.8 and abs(e.center().Z - FOOT_TOP) < 0.8
        ]
        if seam:
            part = part.fillet(2.0, seam)
    except Exception as exc:
        print(f"  L 接缝圆角跳过：{exc}")
    part.label = "SD2_主体"
    part.color = Color(0.93, 0.92, 0.88)
    return part


def _lid_plate(y0: float, z_min: float, z_max: float):
    """和主体同轮廓的一块盖板，只保留 z_min 到 z_max。"""
    with BuildPart() as bp:
        with BuildSketch(xz_at(y0)):
            RectangleRounded(OUTER_W, OUTER_H, R_OUT)
        grow(LID_T)
        if z_max < OUTER_H / 2 - 0.05:
            height = OUTER_H / 2 - z_max + 0.4
            with BuildPart(mode=Mode.SUBTRACT):
                with Locations((0, y0 + LID_T / 2, z_max + height / 2 - 0.2)):
                    Box(OUTER_W + 8, LID_T + 6, height)
        if z_min > -OUTER_H / 2 + 0.05:
            height = z_min - (-OUTER_H / 2) + 0.4
            with BuildPart(mode=Mode.SUBTRACT):
                with Locations((0, y0 + LID_T / 2, -OUTER_H / 2 + height / 2 - 0.2)):
                    Box(OUTER_W + 8, LID_T + 6, height)
    return bp.part


def make_screen_lid():
    """立板后盖。两侧筋条挡住屏幕，中间留出走线。"""
    z_min = FOOT_TOP + 0.20
    rib_y = FRONT + POCKET_D + 0.50
    lid = _lid_plate(UPRIGHT_D, z_min, OUTER_H / 2 + 0.2)
    rib_z0 = POCKET_CZ - POCKET_H / 2 + 4.0
    rib_z1 = POCKET_CZ + POCKET_H / 2 - 10.0
    ribs = None
    for side in (-1, 1):
        rib = Pos(
            side * 20.5,
            (rib_y + UPRIGHT_D) / 2,
            (rib_z0 + rib_z1) / 2,
        ) * Box(6.0, UPRIGHT_D - rib_y, rib_z1 - rib_z0)
        ribs = rib if ribs is None else ribs + rib
    # 定位唇，伸进立板。顶部横梁让开螺丝柱。
    lip = _frame_lip(UPRIGHT_D, z_min + 1.5, OUTER_H / 2 - WALL - 1.0)
    for bx, bz in _upper_bosses():
        clear = Pos(bx, UPRIGHT_D - LIP_D / 2, bz) * Rot(90, 0, 0) * Cylinder(
            BOSS_R + 0.45, LIP_D + 0.6
        )
        lip = lip - clear
    lid = lid + ribs + lip
    holes = None
    for bx, bz in _upper_bosses():
        shank = Pos(bx, UPRIGHT_D + LID_T / 2, bz) * Rot(90, 0, 0) * Cylinder(
            SCREW_CLEAR_D / 2, LID_T + 1.6
        )
        head = Pos(bx, UPRIGHT_D + LID_T - COUNTERBORE_H / 2, bz) * Rot(90, 0, 0) * Cylinder(
            COUNTERBORE_D / 2, COUNTERBORE_H + 0.3
        )
        holes = shank if holes is None else holes + shank
        holes = holes + head
    lid = lid - holes
    lid.label = "SD2_屏后盖"
    lid.color = Color(0.72, 0.75, 0.78)
    return lid


def _frame_lip(y_face: float, z_min: float, z_max: float):
    """盖板内侧一圈定位唇，沿 -Y 伸进主体。"""
    outer_w = INNER_W - 2 * LIP_CLEAR
    outer_h = (z_max - z_min) - 2 * LIP_CLEAR
    cz = (z_min + z_max) / 2
    outer = Pos(0, y_face - LIP_D / 2, cz) * Box(outer_w, LIP_D, outer_h)
    inner = Pos(0, y_face - LIP_D / 2, cz) * Box(
        outer_w - 2 * LIP_T, LIP_D + 0.4, max(outer_h - 2 * LIP_T, 1.0)
    )
    return outer - inner


def make_tail_lid():
    """底座尾盖。Micro-B 孔是圆角矩形。"""
    z_min = -OUTER_H / 2
    z_max = FOOT_TOP
    lid = _lid_plate(FOOT_END, z_min, z_max)
    lip = _frame_lip(FOOT_END, z_min + WALL, z_max - FOOT_TOP_WALL)
    # 唇边避开螺丝柱
    for bx, bz in _foot_bosses():
        clear = Pos(bx, FOOT_END - LIP_D / 2, bz) * Rot(90, 0, 0) * Cylinder(
            BOSS_R + 0.45, LIP_D + 0.6
        )
        lip = lip - clear
    lid = lid + lip
    # 圆角矩形孔，打穿盖板
    with BuildPart() as hole_bp:
        with BuildSketch(xz_at(FOOT_END - 0.2)):
            with Locations((0, USB_CZ)):
                RectangleRounded(USB_W, USB_H, USB_R)
        grow(LID_T + 0.8)
    lid = lid - hole_bp.part
    holes = None
    for bx, bz in _foot_bosses():
        shank = Pos(bx, FOOT_END + LID_T / 2, bz) * Rot(90, 0, 0) * Cylinder(
            SCREW_CLEAR_D / 2, LID_T + 1.6
        )
        head = Pos(bx, FOOT_END + LID_T - COUNTERBORE_H / 2, bz) * Rot(90, 0, 0) * Cylinder(
            COUNTERBORE_D / 2, COUNTERBORE_H + 0.3
        )
        holes = shank if holes is None else holes + shank
        holes = holes + head
    lid = lid - holes
    lid.label = "SD2_尾盖"
    lid.color = Color(0.55, 0.58, 0.62)
    return lid


def make_shim():
    """26 mm 窄板垫在导轨一侧。31 mm 宽的板不用。"""
    with BuildPart() as bp:
        with BuildSketch(Plane.XZ):
            RectangleRounded(40.0, 8.0, 1.2)
        grow(4.6)
    part = bp.part
    part.label = "SD2_窄板垫片"
    part.color = Color(0.93, 0.62, 0.18)
    return part


def keep_largest(part):
    solids = list(part.solids())
    if len(solids) == 1:
        return part
    solids.sort(key=lambda s: s.volume, reverse=True)
    if any(s.volume > 20 for s in solids[1:]):
        raise RuntimeError(
            f"{part.label} 有未连上的零件 "
            + ", ".join(f"{s.volume:.1f}" for s in solids[1:])
        )
    main = solids[0]
    main.label = part.label
    main.color = part.color
    print(f"  {part.label} 去掉碎屑 {[round(s.volume, 2) for s in solids[1:]]}")
    return main


def overlap_volume(a, b) -> float:
    hit = a & b
    try:
        return float(hit.volume)
    except Exception:
        return 0.0


def check_fits(body, screen_lid, tail_lid) -> None:
    screen = Pos(0, FRONT + 0.15 + SCREEN_T / 2, POCKET_CZ) * Box(
        SCREEN_W, SCREEN_T, SCREEN_H
    )
    screen_hit = overlap_volume(body, screen)
    if screen_hit > 0.5:
        raise RuntimeError(f"屏幕和壳体干涉 {screen_hit:.2f} mm³")

    pcb = Pos(0, BOARD_Y0 + PCB_L / 2, PCB_BOTTOM + PCB_T / 2) * Box(
        PCB_W - 0.4, PCB_L - 0.6, PCB_T
    )
    pcb_hit = overlap_volume(body, pcb)
    if pcb_hit > 1.0:
        raise RuntimeError(f"开发板和壳体干涉 {pcb_hit:.2f} mm³")

    # 插头、母座都从尾盖圆孔沿 Y 穿出
    plug = Pos(0, FOOT_END + LID_T / 2, USB_CZ) * Box(6.90, LID_T + 2.4, 1.85)
    plug_hit = overlap_volume(tail_lid, plug)
    if plug_hit > 0.5:
        raise RuntimeError(f"Micro-B 插头被尾盖挡住 {plug_hit:.2f} mm³")
    shell = Pos(0, FOOT_END + LID_T / 2, USB_CZ) * Box(7.60, LID_T + 2.4, 2.60)
    shell_hit = overlap_volume(tail_lid, shell)
    if shell_hit > 0.5:
        raise RuntimeError(f"Micro-B 母座被尾盖挡住 {shell_hit:.2f} mm³")

    # 孔不要被主体挡住（孔在尾盖上，主体在孔前面应留出空当）
    throat = Pos(0, FOOT_END - 1.0, USB_CZ) * Box(7.6, 2.0, 2.6)
    throat_hit = overlap_volume(body, throat)
    if throat_hit > 0.5:
        raise RuntimeError(f"主体挡住了尾盖孔内侧 {throat_hit:.2f} mm³")

    for bx, bz in _upper_bosses():
        rod = Pos(bx, UPRIGHT_D + LID_T / 2, bz) * Rot(90, 0, 0) * Cylinder(0.7, LID_T + 8)
        if overlap_volume(screen_lid, rod) > 0.2:
            raise RuntimeError(f"屏后盖螺丝孔被堵住 @ ({bx:.1f},{bz:.1f})")
    for bx, bz in _foot_bosses():
        rod = Pos(bx, FOOT_END + LID_T / 2, bz) * Rot(90, 0, 0) * Cylinder(0.7, LID_T + 8)
        if overlap_volume(tail_lid, rod) > 0.2:
            raise RuntimeError(f"尾盖螺丝孔被堵住 @ ({bx:.1f},{bz:.1f})")

    print("干涉检查通过")
    print(f"  屏幕 {screen_hit:.3f}  开发板 {pcb_hit:.3f}")
    print(f"  插头 {plug_hit:.3f}  母座 {shell_hit:.3f}  孔内侧 {throat_hit:.3f}")


def report() -> None:
    print("--- L 形 ---")
    print(f"  立板深 {UPRIGHT_D:.1f} mm，总深 {ASSEMBLED_D:.1f} mm")
    print(f"  宽 {OUTER_W:.1f}  高 {OUTER_H:.1f}  底座顶面 Z={FOOT_TOP:.1f}")
    print(f"  开窗 {WIN_W:.1f} x {WIN_H:.1f}")
    print(f"  Micro-B 圆角孔 {USB_W:.1f} x {USB_H:.1f}，圆角 {USB_R:.1f}，中心 Z={USB_CZ:.2f}")
    print(f"  板子 Y {BOARD_Y0:.1f} → {FOOT_END:.1f}")


def _on_bed(part, rotation):
    moved = rotation * part
    moved.label = part.label
    moved.color = part.color
    bb = moved.bounding_box()
    settled = Pos(-bb.min.X, -bb.min.Y, -bb.min.Z) * moved
    settled.label = part.label
    settled.color = part.color
    return settled


def export(parts, path: str) -> None:
    # 主体正面贴床。两块盖板外表面（+Y）贴床。
    oriented = []
    for part, rotation in parts:
        oriented.append(_on_bed(part, rotation))

    placed = []
    cursor = 0.0
    for part in oriented:
        bb = part.bounding_box()
        shift = Pos(cursor - bb.min.X, -bb.min.Y, -bb.min.Z) * part
        shift.label = part.label
        shift.color = part.color
        placed.append(shift)
        cursor += (bb.max.X - bb.min.X) + 8.0

    mesher = Mesher(unit=Unit.MM)
    for part in placed:
        mesher.add_shape(
            part,
            linear_deflection=0.05,
            angular_deflection=0.25,
            part_number=part.label,
        )
    mesher.add_meta_data(
        "https://schemas.bambulab.com",
        "Title",
        "SD2 小电视 L形 Micro-B",
        "string",
        False,
    )
    mesher.write(path)
    print(f"写出 {path}")
    print(f"  三角面 {mesher.triangle_counts}")


def save_previews(body, screen_lid, tail_lid, out_dir: str) -> None:
    os.environ.setdefault("MPLCONFIGDIR", "/tmp/mpl")
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager

    for path in (
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
    ):
        if os.path.isfile(path):
            font_manager.fontManager.addfont(path)
    plt.rcParams["font.family"] = "sans-serif"
    plt.rcParams["font.sans-serif"] = ["Heiti SC", "Songti SC", "PingFang SC"]
    plt.rcParams["axes.unicode_minus"] = False

    asm = body + screen_lid + tail_lid

    def edge_pts(edge):
        n = max(2, int(edge.length / 0.3) + 1)
        return [edge.position_at(i / (n - 1)) for i in range(n)]

    def draw(shape, origin, up, name, title):
        vis, hid = shape.project_to_viewport(origin, up)
        fig, ax = plt.subplots(figsize=(7.2, 6.4), facecolor="white")
        for edge in hid:
            pts = edge_pts(edge)
            ax.plot([p.X for p in pts], [p.Y for p in pts], color="#C4C4C4", lw=0.45, ls="--")
        for edge in vis:
            pts = edge_pts(edge)
            ax.plot([p.X for p in pts], [p.Y for p in pts], color="#1a1a1a", lw=1.0)
        ax.set_aspect("equal")
        ax.axis("off")
        ax.set_title(title, fontsize=13, color="#222", pad=8)
        fig.savefig(os.path.join(out_dir, name), dpi=150, bbox_inches="tight", facecolor="white")
        plt.close()

    mid_y = ASSEMBLED_D / 2
    draw(asm, (0, -300, 0), (0, 0, 1), "SD2_preview_front.png", "正面")
    draw(asm, (0, 400, 0), (0, 0, 1), "SD2_preview_back.png", "背面")
    draw(asm, (320, mid_y, 0), (0, 0, 1), "SD2_preview_side.png", "侧面 · L")
    draw(asm, (-220, -260, 180), (0, 0, 1), "SD2_preview_iso.png", "装配")
    cut = asm - Pos(40, mid_y, 0) * Box(80, ASSEMBLED_D + 10, OUTER_H + 10)
    draw(cut, (200, -240, 140), (0, 0, 1), "SD2_preview_section.png", "剖视")
    draw(tail_lid, (0, FOOT_END + 80, USB_CZ), (0, 0, 1), "SD2_preview_bottom.png", "尾盖")


def main() -> None:
    here = os.path.dirname(os.path.abspath(__file__))
    report()
    print("生成主体…")
    body = make_body()
    print("生成屏后盖…")
    screen_lid = make_screen_lid()
    print("生成尾盖…")
    tail_lid = make_tail_lid()
    print("生成垫片…")
    shim = make_shim()
    check_fits(body, screen_lid, tail_lid)
    body = keep_largest(body)
    screen_lid = keep_largest(screen_lid)
    tail_lid = keep_largest(tail_lid)
    shim = keep_largest(shim)
    export(
        [
            (body, Rot(90, 0, 0)),
            (screen_lid, Rot(-90, 0, 0)),
            (tail_lid, Rot(-90, 0, 0)),
            (shim, Rot(0, 0, 0)),
        ],
        os.path.join(here, "SD2_小电视_2.4寸_ESP8266.3mf"),
    )
    print("渲染预览…")
    save_previews(body, screen_lid, tail_lid, here)


if __name__ == "__main__":
    main()
