// Per-frame records of the deferred (visibility buffer) path.
// Must match RenderVsDeferredClustered::DeferredDrawRecord / DeferredGeometryRecord (std430).

// DeferredDrawRecord.flags: same bits as the INSTANCE_DATA_FLAG_MULTIMESH* flags of the scene shader.
#define DRAW_FLAG_MULTIMESH_INDIRECT (1u << 2)
#define DRAW_FLAG_MULTIMESH (1u << 12)
#define DRAW_FLAG_MULTIMESH_FORMAT_2D (1u << 13)
#define DRAW_FLAG_MULTIMESH_HAS_COLOR (1u << 14)
#define DRAW_FLAG_MULTIMESH_HAS_CUSTOM_DATA (1u << 15)

// One per draw group (lone surface, multimesh or implicitly instanced objects) of the opaque list, indexed by the group's first element (surface_submission_idx, the
// upper 16 bits of visibility .y). Entries of non-first elements are zero.
struct DrawRecord {
	uvec2 instance_addr; // Multimesh buffer address, 0 otherwise.
	uint material_slot; // Index into the per-frame material slot table.
	uint geometry_idx; // Index into geometry_records.
	uint multimesh_offset; // Current-frame offset (in instances) into a double-buffered multimesh buffer.
	uint flags; // DRAW_FLAG_*.
	uint instance_count; // Instances drawn by the group (0 marks an entry that is not a group start).
	uint pad;
};

#define GEOM_FLAG_INDEXED (1u << 0)
#define GEOM_FLAG_INDEX16 (1u << 1)
#define GEOM_FLAG_STRIP (1u << 2) // Triangle strip.
#define GEOM_FLAG_COMPRESSED_READ (1u << 3) // UNORM16 positions/normals in the stream that is read.
#define GEOM_FLAG_COMPRESSED_UV (1u << 4)
#define GEOM_FLAG_HAS_NORMAL (1u << 5)
#define GEOM_FLAG_HAS_UV (1u << 6)
#define GEOM_FLAG_NOT_TRIANGLES (1u << 7) // Points/lines: primitive ids are not triangles, decode is skipped.

// One per (mesh surface, LOD, mesh instance) used by the opaque list this frame.
struct GeometryRecord {
	uvec2 vertex_addr; // Position (+ normal/tangent) stream; the mesh-instance output buffer for skinned/blend shape instances.
	uvec2 attrib_addr; // Attribute stream (color, UV, UV2, custom).
	uvec2 index_addr; // Index buffer of the drawn LOD, 0 if not indexed.
	uint vertex_count;
	uint index_count; // Of the drawn LOD.
	uint flags; // GEOM_FLAG_*.
	uint position_stride; // Bytes.
	uint normal_stride; // Bytes, normal stream starts at position_stride * vertex_count.
	uint attrib_stride; // Bytes.
	uint uv_offset; // Bytes inside attrib_stride.
	uint pad;
};
