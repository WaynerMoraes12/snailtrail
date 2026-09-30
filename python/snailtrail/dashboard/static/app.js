document.addEventListener("click", async (event) => {
    const button = event.target.closest("[data-copy]");
    if (!button) return;
    const code = button.parentElement.querySelector("pre");
    try {
        await navigator.clipboard.writeText(code.textContent);
        button.textContent = "Copied";
    } catch {
        button.textContent = "Select and copy";
    }
    setTimeout(() => {
        button.textContent = "Copy";
    }, 1600);
});
