// main.js - menu toggles, trivia rotation, faq accordion

// Reusable menu attachment
function attachMenu(btnId, dropId) {
  const btn = document.getElementById(btnId);
  const drop = document.getElementById(dropId);
  if (!btn || !drop) return;
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    drop.classList.toggle('hidden');
    drop.setAttribute('aria-hidden', drop.classList.contains('hidden'));
  });
  document.addEventListener('click', () => {
    if (!drop.classList.contains('hidden')) {
      drop.classList.add('hidden');
      drop.setAttribute('aria-hidden', 'true');
    }
  });
}

// Attach menus on pages (IDs used in templates)
attachMenu('menuBtn', 'menuDropdown');
attachMenu('menuBtnC', 'menuDropdownC');
attachMenu('menuBtnF', 'menuDropdownF');

// Trivia facts (rotate every 2 minutes)
const trivia = [
  "Hydroponics can use up to 90% less water than conventional soil farming.",
  "NFT (Nutrient Film Technique) is common for leafy greens and uses a thin nutrient flow.",
  "Most leafy greens prefer pH 5.5–6.5 for optimal nutrient uptake.",
  "Hydroponic produce often grows faster because nutrients are delivered directly to roots.",
  "TDS measures dissolved solids in ppm; it helps track nutrient concentration.",
  "LED grow lights tuned to red/blue wavelengths improve photosynthesis efficiency."
];

let triviaIndex = 0;
function rotateTrivia() {
  const el = document.getElementById('triviaText');
  if (!el) return;
  el.innerText = trivia[triviaIndex];
  triviaIndex = (triviaIndex + 1) % trivia.length;
}
rotateTrivia();
setInterval(rotateTrivia, 120000); // every 2 minutes

// FAQ accordion
document.addEventListener('DOMContentLoaded', () => {
  document.querySelectorAll('.faq-q').forEach(btn => {
    btn.addEventListener('click', () => {
      const a = btn.nextElementSibling;
      const open = a.style.display === 'block';
      document.querySelectorAll('.faq-a').forEach(x => x.style.display = 'none');
      a.style.display = open ? 'none' : 'block';
    });
  });
});
