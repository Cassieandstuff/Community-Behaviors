"""Community Behaviors — Control Rig Importer (Blender addon).

Imports a portable CB control rig straight from a `control/rig.yaml` (emitted by
`havok-core-cli control-decompile`, and — once wired — carried in the deployed load order).
Scans a root folder for rigs, offers an actor picker, and builds a Blender armature with each
control node placed at its recovered WORLD pose and tagged by pose source.

The heavy lifting (parse + forward-kinematics compose + scan) lives in `rigcore`, which has NO
Blender/PyYAML dependency and is validated headlessly against the whole vanilla bestiary. This
file only maps that engine-agnostic result onto bpy armature data.
"""

bl_info = {
    "name": "Community Behaviors — Control Rig Importer",
    "author": "Community Behaviors",
    "version": (0, 1, 0),
    "blender": (3, 6, 0),
    "location": "View3D > Sidebar (N) > CB",
    "description": "Import a portable control rig from a CB control/rig.yaml load order",
    "category": "Import-Export",
}

import os

import bpy  # type: ignore
import mathutils  # type: ignore
from bpy.props import EnumProperty, StringProperty, FloatProperty, BoolProperty  # type: ignore
from bpy_extras.io_utils import ExportHelper  # type: ignore

from . import rigcore

# Cache of the last scan: label -> path (an EnumProperty items callback must be cheap + stable).
_RIG_CACHE = []          # [(label, path), ...]
_RIG_CACHE_ROOT = None

# Bone color palettes (Blender 4.x theme slots) by pose source — a quick visual read of what's
# authoritative vs derived vs still-unresolved.
_SOURCE_COLOR = {
    "skeleton": "THEME01",       # green-ish: authoritative referencePose
    "nif": "THEME04",            # blue: from skeleton.nif
    "derived": "THEME09",        # yellow: convention default (refine me)
    "hint:convention": "THEME02",  # red: unresolved, needs authoring
    "hint:nif": "THEME02",
    "hint:aux": "THEME03",
}


def _refresh_cache(root):
    global _RIG_CACHE, _RIG_CACHE_ROOT
    _RIG_CACHE = rigcore.scan_load_order(root) if root and os.path.isdir(root) else []
    _RIG_CACHE_ROOT = root


def _rig_items(self, context):
    root = context.scene.cb_rig_root
    if root != _RIG_CACHE_ROOT:
        _refresh_cache(root)
    if not _RIG_CACHE:
        return [("__none__", "(scan a folder first)", "")]
    return [(path, label, path) for label, path in _RIG_CACHE]


class CB_OT_scan(bpy.types.Operator):
    bl_idname = "cb.scan_rigs"
    bl_label = "Scan for Control Rigs"
    bl_description = "Recursively find control/rig.yaml under the root folder"

    def execute(self, context):
        _refresh_cache(context.scene.cb_rig_root)
        self.report({"INFO"}, f"Found {len(_RIG_CACHE)} control rig(s)")
        return {"FINISHED"}


class CB_OT_import(bpy.types.Operator):
    bl_idname = "cb.import_rig"
    bl_label = "Import Control Rig"
    bl_description = "Build an armature from the selected actor's control rig"

    def execute(self, context):
        scn = context.scene
        path = scn.cb_rig_selected
        if not path or path == "__none__" or not os.path.isfile(path):
            self.report({"ERROR"}, "No rig selected (scan a folder and pick an actor)")
            return {"CANCELLED"}

        rig = rigcore.load_rig(path)
        nodes = rigcore.flatten(rig)
        if not nodes:
            self.report({"ERROR"}, "Rig has no nodes")
            return {"CANCELLED"}

        label = rigcore.actor_label_from_path(path)
        length = scn.cb_bone_length
        uscale = scn.cb_import_scale

        arm_data = bpy.data.armatures.new(f"CB_{label}")
        arm_obj = bpy.data.objects.new(f"CB_{label}", arm_data)
        context.collection.objects.link(arm_obj)
        context.view_layer.objects.active = arm_obj
        arm_obj.select_set(True)

        bpy.ops.object.mode_set(mode="EDIT")
        edit = {}
        try:
            for n in nodes:                       # flatten() is parents-before-children
                eb = arm_data.edit_bones.new(n.name)
                t = n.world["translation"]
                q = n.world["rotation"]           # [x, y, z, w]
                head = mathutils.Vector((t[0] * uscale, t[1] * uscale, t[2] * uscale))
                quat = mathutils.Quaternion((q[3], q[0], q[1], q[2]))  # bpy wants (w, x, y, z)
                mat = quat.to_matrix().to_4x4()
                mat.translation = head
                eb.head = head
                eb.tail = head + mathutils.Vector((0.0, length, 0.0))
                eb.matrix = mat                    # sets head/direction/roll; keeps the length above
                if n.parent and n.parent in edit:
                    eb.parent = edit[n.parent]
                    eb.use_connect = False
                edit[n.name] = eb
        finally:
            bpy.ops.object.mode_set(mode="OBJECT")

        # Tag each bone with pose provenance (custom props + a color palette where supported).
        for n in nodes:
            b = arm_data.bones.get(n.name)
            if not b:
                continue
            b["cb_pose_source"] = n.source
            b["cb_posed"] = n.posed
            if n.handles:
                b["cb_handles"] = ", ".join(h.get("variant", "?") for h in n.handles)
            try:
                b.color.palette = _SOURCE_COLOR.get(n.source, "DEFAULT")
            except Exception:
                pass

        st = rigcore.rig_stats(rig)
        self.report(
            {"INFO"},
            f"Imported '{label}': {len(nodes)} nodes "
            f"(skeleton {st.get('posedFromSkeleton')}, nif {st.get('posedFromNif')}, "
            f"derived {st.get('derivedByConvention')})",
        )
        return {"FINISHED"}


class CB_OT_export(bpy.types.Operator, ExportHelper):
    bl_idname = "cb.export_rig"
    bl_label = "Export Control Rig"
    bl_description = "Write the active armature to a CB control/rig.yaml (authored control rig)"

    filename_ext = ".yaml"
    filter_glob: StringProperty(default="*.yaml", options={"HIDDEN"})  # type: ignore

    def invoke(self, context, event):
        arm = context.active_object
        if arm and arm.type == "ARMATURE":
            self.filepath = "rig.yaml"
        return ExportHelper.invoke(self, context, event)

    def execute(self, context):
        arm = context.active_object
        if not arm or arm.type != "ARMATURE":
            self.report({"ERROR"}, "Active object is not an armature")
            return {"CANCELLED"}

        uscale = context.scene.cb_import_scale or 1.0
        entries = []
        for bone in arm.data.bones:
            # bone.matrix_local is the bone's REST matrix in ARMATURE space (world, for our purposes).
            loc, quat, _scale = bone.matrix_local.decompose()  # quat is (w, x, y, z)
            entries.append({
                "name": bone.name,
                "parent": bone.parent.name if bone.parent else None,
                "world": {
                    "translation": [loc.x / uscale, loc.y / uscale, loc.z / uscale],
                    "rotation": [quat.x, quat.y, quat.z, quat.w],  # -> CB [x, y, z, w]
                    "scale": [1.0, 1.0, 1.0],  # bone rest carries no scale
                },
            })
        if not entries:
            self.report({"ERROR"}, "Armature has no bones")
            return {"CANCELLED"}

        rig = rigcore.worlds_to_rig(entries, source="authored-blender:" + arm.name)
        text = rigcore.emit_rig_yaml(rig)
        with open(self.filepath, "w", encoding="utf-8") as fh:
            fh.write(text)
        self.report({"INFO"}, f"Exported {len(entries)} bone(s) -> {self.filepath}")
        return {"FINISHED"}


class CB_PT_panel(bpy.types.Panel):
    bl_label = "Control Rig Importer"
    bl_idname = "CB_PT_control_rig"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "CB"

    def draw(self, context):
        layout = self.layout
        scn = context.scene
        layout.prop(scn, "cb_rig_root")
        layout.operator("cb.scan_rigs", icon="VIEWZOOM")
        row = layout.row()
        row.enabled = bool(_RIG_CACHE)
        row.prop(scn, "cb_rig_selected", text="Actor")
        col = layout.column(align=True)
        col.prop(scn, "cb_bone_length")
        col.prop(scn, "cb_import_scale")
        layout.operator("cb.import_rig", icon="ARMATURE_DATA")
        if _RIG_CACHE:
            layout.label(text=f"{len(_RIG_CACHE)} rig(s) found", icon="INFO")

        layout.separator()
        col = layout.column()
        col.label(text="Export (active armature):")
        col.operator("cb.export_rig", icon="EXPORT")


_CLASSES = (CB_OT_scan, CB_OT_import, CB_OT_export, CB_PT_panel)


def register():
    bpy.types.Scene.cb_rig_root = StringProperty(
        name="Load Order / Rig Root",
        description="Folder to scan for control/rig.yaml (deployed data, a mod, or a cb-diffs tree)",
        subtype="DIR_PATH",
        default="",
    )
    bpy.types.Scene.cb_rig_selected = EnumProperty(
        name="Actor", description="Which actor's control rig to import", items=_rig_items,
    )
    bpy.types.Scene.cb_bone_length = FloatProperty(
        name="Bone Length", description="Display length for each control bone (native units)",
        default=3.0, min=0.01, max=100.0,
    )
    bpy.types.Scene.cb_import_scale = FloatProperty(
        name="Import Scale", description="Uniform scale applied to positions on import",
        default=1.0, min=0.0001, max=1000.0,
    )
    for c in _CLASSES:
        bpy.utils.register_class(c)


def unregister():
    for c in reversed(_CLASSES):
        bpy.utils.unregister_class(c)
    del bpy.types.Scene.cb_rig_root
    del bpy.types.Scene.cb_rig_selected
    del bpy.types.Scene.cb_bone_length
    del bpy.types.Scene.cb_import_scale


if __name__ == "__main__":
    register()
