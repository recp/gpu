const previews = document.querySelectorAll(".preview-link iframe");

if ("IntersectionObserver" in window) {
  const observer = new IntersectionObserver((entries) => {
    for (const entry of entries) {
      entry.target.style.visibility = entry.isIntersecting ? "visible" : "hidden";
    }
  }, { rootMargin: "160px" });

  previews.forEach((preview) => observer.observe(preview));
}
