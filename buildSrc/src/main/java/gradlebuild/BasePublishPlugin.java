package gradlebuild;

import org.gradle.api.publish.Publication;

public class BasePublishPlugin {
    public static final String LOCAL_FILE_REPOSITORY_NAME = "LocalFile";

    public static String publishTaskName(Publication publication, String repository) {
        return "publish" + capitalize(publication.getName()) + "PublicationTo" + repository + "Repository";
    }

    static String capitalize(String name) {
        StringBuilder builder = new StringBuilder(name);
        builder.setCharAt(0, Character.toUpperCase(builder.charAt(0)));
        return builder.toString();
    }

    static boolean isMainPublication(Publication publication) {
        return publication.getName().equals("main");
    }
}
