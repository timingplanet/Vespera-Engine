(() => {
  const node = document.querySelector('[data-year]');
  if (node) node.textContent = new Date().getFullYear();
})();
