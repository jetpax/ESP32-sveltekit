<script lang="ts">
  import CodeMirror from "svelte-codemirror-editor";
  import { javascript } from "@codemirror/lang-javascript";
  import { oneDark } from "@codemirror/theme-one-dark"; // Optional: Dark theme

  let value = "// Start typing your JavaScript code here...\nconsole.log('Hello, world!');";
</script>

<CodeMirror bind:value={value} lang={javascript()} theme={oneDark} />

<button on:click={() => console.log("Saving content:", value)} class="btn btn-primary mt-4">Save File</button>