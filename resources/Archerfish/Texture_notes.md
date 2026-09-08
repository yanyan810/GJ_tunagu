# Archerfish texture

Created using the built-in image generation tool, matching the shrimp's simple low-poly style.

Use `Archerfish_textured.gltf` with `Archerfish_textured.bin` and the new `Archerfish_Texture.png`. The original model and `Torpedo_Texture.png` are unchanged. The supplied UV guide differs from the GLTF UV coordinates; the final texture was generated from the extracted layout, then the variant model UVs were adjusted to align the body stripes along the fish. Original geometry is unchanged. `Archerfish_textured.blend` and `Archerfish_preview.png` provide a material preview. Game code is unchanged.

Final generation prompt:

Create a flat low-poly archerfish base-color UV texture by painting EXACTLY over the UV layout in image 1. Image 2 is the desired palette/style reference ONLY, do NOT copy its layout. Preserve image 1 exact UV island placement, silhouettes, sizes, orientations, and positions. The two large tall islands on the LEFT occupying x0-230 and x230-445, y185-1024 are the two fish body sides, rotated upright: paint them warm silver cream with 4 charcoal transverse tapered bands going HORIZONTALLY across each tall island at y440,570,700,830, matching both sides. Other smaller islands on right are fins and narrow body sections, use restrained olive-grey and muted amber. Simple restrained flat low-poly colors like a handmade game model, no detailed scales, no realism. Paint over ALL internal black triangulation and wireframe lines completely. Very important: fill all areas OUTSIDE the islands with a uniform medium warm grey-olive instead of black to avoid sampling seams. Do not add eyes without knowing their exact UV position. No labels, text, watermark or 3D render. Output square texture only.

