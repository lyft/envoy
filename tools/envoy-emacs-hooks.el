(defun envoy-rotate-files ()
  "Rotate the current buffer based on envoy cc file patterns"
  (interactive)
  (find-file
   (car (split-string (shell-command-to-string
                       (concat "find_related_envoy_files.py " (buffer-file-name)))))))

(defun envoy-setup-hooks ()
  "Function to run to set up Emacs customizations for editing in the Envoy codebase"
  (local-set-key (quote [C-tab]) 'envoy-rotate-files))

(add-hook 'c-mode-common-hook 'envoy-setup-hooks t)
