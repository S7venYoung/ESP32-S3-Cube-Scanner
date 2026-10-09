# Original mockup dojo background

Built-in image generation edit of the user-approved mockup. Visible dojo artwork
was retained/reconstructed, removing fighters and HUD. Not an untouched crop:
occluded areas had to be reconstructed. The accepted source is arcade-dojo-v2.png;
the first independently generated dojo was discarded. RGB565 conversion only,
236x104, 49,088 bytes. The PNG stays in the workspace; the firmware-ready .inc is
included in GitHub so firmware compilation does not require image tools.

Prompt:

Use case: precise-object-edit. Image 1 is the EDIT TARGET, not a loose style reference. Extract only the original dojo background inside the left large display's fight area (approximately x=66..936,y=230..695). Remove Ryu, Ken, central VS, foreground TODAY TOKENS banner, all HUD/labels and cabinet; reconstruct ONLY the dojo surfaces they occlude. Preserve the visible background's exact architectural layout, central vertical 武 scroll, muted blue-gray wood pillars, rope swags, amber lanterns and warm glow, same painterly non-pixel style and perspective. Do NOT redesign into a new symmetrical polished dojo or add racks/weapons/props. Return a clean opaque rectangular background only, wide 2.27:1; minimal floor consistent with original scene. No UI, fighters, VS, numbers, frame, cabinet. Maintain exact original visible colors and placement; intended mechanical downsample 236x104.
