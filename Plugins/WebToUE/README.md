# WebToUE 0.1 Developer Preview

WebToUE imports HTML and CSS into an Unreal asset and renders it through Slate. It does not use CEF, WebBrowser, or a JavaScript runtime.

## Quick start

1. Keep source files under the project's `WebUI` directory.
2. Drag an `.html` file into the Content Browser. Linked CSS files are compiled into the resulting `WebToUEDocument` asset.
3. Add **WebToUE View** to a Widget Blueprint and assign the document.
4. Optionally call `Set Data Context` with a UObject and bind root UPROPERTY fields with `data-ue-bind-text`, `data-ue-bind-visible`, or `data-ue-bind-enabled`.
5. Handle `On UI Event`; buttons emit the value of `data-ue-on-click` and their `id`.

Saving the imported HTML or a linked CSS file triggers a debounced reimport and refreshes live views. Packaged builds read only the cooked document asset. Containers using `overflow: auto` or `overflow: scroll` clip overflowing descendants and respond to the mouse wheel.

## First-version limits

The supported authoring subset is intentionally small: flex and absolute layout, box sizing, solid color/rounded boxes, images, wrapped plain text, clipping and basic vertical wheel scrolling, mouse interaction, Tab focus, and instant interaction pseudo-classes. JavaScript, forms, visible scrollbars, drag/touch scrolling, rich text, animations, gradients, shadows, transforms, touch, gamepad, and world-space UI are not included yet.

`img src` uses an Unreal soft object path such as `/Game/UI/T_Logo.T_Logo`. Font family names can be mapped to Unreal font objects in **Project Settings → WebToUE**.
