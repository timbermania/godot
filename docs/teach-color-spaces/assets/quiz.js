// quiz.js — shared self-check widget for the Color Spaces course.
// Markup contract:
//   <div class="quiz" data-answer="1">
//     <p class="q">Question?</p>
//     <div class="opts">
//       <button class="opt">option a</button>
//       <button class="opt">option b</button>   <!-- index 1 = correct here -->
//       <button class="opt">option c</button>
//     </div>
//     <p class="feedback" data-good="Why it's right." data-bad="Nudge toward the idea."></p>
//   </div>
// Retrieval practice: no answer is revealed until the learner commits to a choice.

document.querySelectorAll('.quiz').forEach(function (quiz) {
  var answer = parseInt(quiz.getAttribute('data-answer'), 10);
  var opts = Array.prototype.slice.call(quiz.querySelectorAll('.opt'));
  var fb = quiz.querySelector('.feedback');
  var done = false;

  opts.forEach(function (btn, i) {
    btn.addEventListener('click', function () {
      if (done) return;
      done = true;
      opts.forEach(function (b, j) {
        b.disabled = true;
        if (j === answer) b.classList.add('correct');
      });
      if (i !== answer) btn.classList.add('wrong');
      var ok = i === answer;
      fb.textContent = (ok ? '✓ ' : '✗ ') + fb.getAttribute(ok ? 'data-good' : 'data-bad');
      fb.classList.add(ok ? 'good' : 'bad');
    });
  });
});
