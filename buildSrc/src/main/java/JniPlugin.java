import org.gradle.api.Plugin;
import org.gradle.api.Project;
import org.gradle.api.file.Directory;
import org.gradle.api.provider.Provider;
import org.gradle.api.tasks.TaskContainer;
import org.gradle.api.tasks.TaskProvider;
import org.gradle.api.tasks.compile.JavaCompile;
import org.gradle.language.cpp.tasks.CppCompile;

public abstract class JniPlugin implements Plugin<Project> {

    @Override
    public void apply(Project project) {
        project.getPluginManager().withPlugin("java", plugin -> {
            JniExtension jniExtension = project.getExtensions().create("jni", JniExtension.class);
            jniExtension.getGeneratedHeadersDirectory().convention(project.getLayout().getBuildDirectory().dir("generated/jni-headers"));
            TaskContainer tasks = project.getTasks();
            TaskProvider<JavaCompile> compileJavaProvider = tasks.named("compileJava", JavaCompile.class);
            configureCompileJava(compileJavaProvider, jniExtension);
            configureIncludePath(tasks, compileJavaProvider.flatMap(t -> t.getOptions().getHeaderOutputDirectory()));
        });
    }

    void configureCompileJava(
        TaskProvider<JavaCompile> compileJavaProvider,
        JniExtension jniExtension
    ) {
        compileJavaProvider.configure(compileJava -> {
            // TODO: This shouldn't be necessary since getHeaderOutputDirectory is already marked as a @OutputDirectory.
            compileJava.getOutputs().dir(compileJava.getOptions().getHeaderOutputDirectory());

            compileJava.getOptions().getHeaderOutputDirectory().set(jniExtension.getGeneratedHeadersDirectory());
            // Cannot do incremental header generation
            compileJava.getOptions().setIncremental(false);
        });
    }

    private void configureIncludePath(TaskContainer tasks, Provider<Directory> generatedHeaderDirectory) {
        tasks.withType(CppCompile.class).configureEach(task -> {
            task.includes(generatedHeaderDirectory);
        });
    }
}
